#include "fluid/Persistent.hpp"

#include "phox/pipeline/PipelineDesc.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/GraphExecutor.hpp"
#include "phox/vulkan/PipelineCache.hpp"

#include <array>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace fluid {

using namespace phox;

PersistentBuffer::PersistentBuffer() = default;
PersistentBuffer::~PersistentBuffer() = default;
PersistentBuffer::PersistentBuffer(PersistentBuffer &&) noexcept = default;
PersistentBuffer &PersistentBuffer::operator=(PersistentBuffer &&) noexcept = default;

void PersistentBuffer::create(vk::Device &device, std::uint64_t size, BufferUsageMask usage, std::string name) {
    m_buffer = device.makeBuffer({size, usage, MemoryLocation::DeviceLocal, name});
    m_size = size;
    m_name = std::move(name);
}

void PersistentBuffer::destroy() {
    m_buffer.reset();
    m_size = 0;
}

BufferHandle PersistentBuffer::import(vk::FrameFeatureContext &context, bool contents) const {
    if (!m_buffer) {
        throw std::logic_error("persistent buffer '" + m_name + "' was not created");
    }
    const BufferHandle handle =
        context.graph.importBuffer(m_name, BufferDesc{m_size}, contents ? BufferUsage::StorageWrite : BufferUsage::Undefined,
                                   BufferUsage::StorageWrite, Queue::AsyncCompute);
    context.executor.bindBuffer(handle, m_buffer->handle());
    return handle;
}

namespace {

PipelineDesc gridPipelineDesc(const std::string &shader) {
    PipelineDesc desc;
    desc.name = shader;
    desc.computeShader = shader;
    desc.pushConstants = {{kComputeStage, 0, 128}};
    desc.setLayouts = {SetLayout::ComputeStorage};
    return desc;
}

constexpr std::uint32_t kGroupSize = 8; // local_size in shaders/fluid_common.glsl

} // namespace

void dispatchGrid(vk::FrameFeatureContext &context, const std::string &name, const std::string &shader,
                  std::initializer_list<BufferHandle> reads, std::initializer_list<BufferHandle *> writes,
                  const void *push, std::size_t pushSize, std::uint32_t width, std::uint32_t height) {
    if (reads.size() + writes.size() > 4 || pushSize > 128) {
        throw std::invalid_argument("pass '" + name + "': at most 4 buffers and 128 bytes of push constants");
    }
    const vk::Pipeline &pipeline = context.pipelines.get(gridPipelineDesc(shader));
    const VkDescriptorSetLayout setLayout = context.pipelines.setLayout(SetLayout::ComputeStorage);
    std::array<std::uint8_t, 128> constants{};
    std::memcpy(constants.data(), push, pushSize);
    const std::uint32_t groupsX = (width + kGroupSize - 1) / kGroupSize;
    const std::uint32_t groupsY = (height + kGroupSize - 1) / kGroupSize;

    // Bindings in order: the reads, then the new versions of the writes.
    const std::vector<BufferHandle> &bound = context.graph.addPass<std::vector<BufferHandle>>(
        name, PassType::Compute,
        [&](PassBuilder &builder, std::vector<BufferHandle> &data) {
            for (BufferHandle read : reads) data.push_back(builder.read(read, BufferUsage::StorageRead));
            for (BufferHandle *write : writes) data.push_back(builder.write(*write, BufferUsage::StorageWrite));
            builder.async();
        },
        [&pipeline, setLayout, constants, pushSize, groupsX, groupsY](const std::vector<BufferHandle> &data,
                                                                       PassContext &passContext) {
            auto &ctx = vk::PassContext::from(passContext);
            const auto &table = ctx.device().table();
            const VkCommandBuffer cmd = ctx.cmd();
            const VkDescriptorSet set = ctx.frame().descriptors().allocate(setLayout);
            std::vector<VkDescriptorBufferInfo> infos;
            infos.reserve(data.size());
            for (BufferHandle handle : data) infos.push_back({ctx.buffer(handle), 0, VK_WHOLE_SIZE});
            std::vector<VkWriteDescriptorSet> descriptorWrites(data.size());
            for (std::size_t i = 0; i < data.size(); ++i) {
                descriptorWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                descriptorWrites[i].dstSet = set;
                descriptorWrites[i].dstBinding = static_cast<std::uint32_t>(i);
                descriptorWrites[i].descriptorCount = 1;
                descriptorWrites[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                descriptorWrites[i].pBufferInfo = &infos[i];
            }
            table.vkUpdateDescriptorSets(ctx.device().handle(), static_cast<std::uint32_t>(descriptorWrites.size()),
                                         descriptorWrites.data(), 0, nullptr);
            table.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
            table.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0, 1, &set, 0, nullptr);
            table.vkCmdPushConstants(cmd, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                     static_cast<std::uint32_t>(pushSize), constants.data());
            table.vkCmdDispatch(cmd, groupsX, groupsY, 1);
        });
    std::size_t i = reads.size();
    for (BufferHandle *write : writes) *write = bound[i++];
}

void clearBuffer(vk::FrameFeatureContext &context, const std::string &name, BufferHandle &buffer) {
    buffer = context.graph.addPass<BufferHandle>(
        name, PassType::Compute,
        [&](PassBuilder &builder, BufferHandle &out) {
            out = builder.write(buffer, BufferUsage::TransferDst);
            builder.async();
        },
        [](const BufferHandle &target, PassContext &passContext) {
            auto &ctx = vk::PassContext::from(passContext);
            ctx.device().table().vkCmdFillBuffer(ctx.cmd(), ctx.buffer(target), 0, VK_WHOLE_SIZE, 0);
        });
}

} // namespace fluid
