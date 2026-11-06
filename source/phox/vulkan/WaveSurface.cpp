#include "phox/vulkan/WaveSurface.hpp"

#include "phox/pipeline/PipelineDesc.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/GraphExecutor.hpp"
#include "phox/vulkan/PipelineCache.hpp"

#include <cmath>
#include <cstdint>

namespace phox::vk {

namespace {

struct WavePushConstants {
    std::uint32_t resolution;
    float size;
    float amplitude;
    float wavenumber;
    float time;
};

PipelineDesc waveSurfacePipelineDesc() {
    PipelineDesc desc;
    desc.name = "wave surface";
    desc.computeShader = "wavesurface.comp";
    desc.pushConstants = {{kComputeStage, 0, sizeof(WavePushConstants)}};
    desc.setLayouts = {SetLayout::ComputeStorage};
    return desc;
}

constexpr std::uint32_t kGroupSize = 8; // local_size_x and _y in wavesurface.comp

} // namespace

Mesh WaveSurface::referenceMesh(const Settings &settings, float time) {
    const float a = settings.amplitude;
    const float k = settings.wavenumber;
    const auto vertex = [&](vec2 p) {
        const float u = k * p.x + time;
        const float v = k * p.y + 0.7f * time;
        const vec2 slope{a * k * std::cos(u) * std::cos(v), -a * k * std::sin(u) * std::sin(v)};
        const vec3 normal = glm::normalize(vec3(-slope.x, 1.0f, -slope.y));
        vec3 tangent{1.0f, slope.x, 0.0f};
        tangent = glm::normalize(tangent - normal * glm::dot(normal, tangent));
        Vertex out;
        out.position = {p.x, a * std::sin(u) * std::cos(v), p.y};
        out.normal = normal;
        out.uv = p / settings.size + 0.5f;
        out.tangent = vec4(tangent, -1.0f);
        return out;
    };
    const std::uint32_t n = settings.resolution;
    const float step = settings.size / static_cast<float>(n);
    std::vector<Vertex> vertices;
    vertices.reserve(static_cast<std::size_t>(n) * n * 6);
    for (std::uint32_t z = 0; z < n; ++z) {
        for (std::uint32_t x = 0; x < n; ++x) {
            const vec2 p00 = vec2(-0.5f * settings.size) + vec2(static_cast<float>(x), static_cast<float>(z)) * step;
            const vec2 p10 = p00 + vec2(step, 0.0f);
            const vec2 p01 = p00 + vec2(0.0f, step);
            const vec2 p11 = p00 + vec2(step, step);
            for (const vec2 &p : {p01, p11, p10, p01, p10, p00}) vertices.push_back(vertex(p));
        }
    }
    std::vector<std::uint32_t> indices(vertices.size());
    for (std::size_t i = 0; i < indices.size(); ++i) indices[i] = static_cast<std::uint32_t>(i);
    return Mesh(std::move(vertices), std::move(indices));
}

void WaveSurface::addPasses(FrameFeatureContext &context, std::vector<GeneratedDraw> &draws) {
    RenderGraph &graph = context.graph;
    const std::uint32_t n = m_settings.resolution;
    const BufferHandle vertices =
        graph.createBuffer("wave vertices", BufferDesc{static_cast<std::uint64_t>(n) * n * 6 * sizeof(Vertex)});
    const BufferHandle indirect = graph.createBuffer("wave draw", BufferDesc{sizeof(VkDrawIndirectCommand)});
    const Pipeline &pipeline = context.pipelines.get(waveSurfacePipelineDesc());
    const VkDescriptorSetLayout setLayout = context.pipelines.setLayout(SetLayout::ComputeStorage);
    const WavePushConstants constants{n, m_settings.size, m_settings.amplitude, m_settings.wavenumber, m_time};

    struct Data {
        BufferHandle vertices;
        BufferHandle indirect;
    };
    const bool async = m_settings.async;
    const Data &out = graph.addPass<Data>(
        "wave surface", PassType::Compute,
        [&](PassBuilder &builder, Data &data) {
            data.vertices = builder.write(vertices, BufferUsage::StorageWrite);
            data.indirect = builder.write(indirect, BufferUsage::StorageWrite);
            if (async) builder.async();
        },
        [&pipeline, setLayout, constants](const Data &data, phox::PassContext &passContext) {
            auto &ctx = PassContext::from(passContext);
            const auto &table = ctx.device().table();
            const VkCommandBuffer cmd = ctx.cmd();
            const VkDescriptorSet set = ctx.frame().descriptors().allocate(setLayout);
            const VkDescriptorBufferInfo buffers[2] = {{ctx.buffer(data.vertices), 0, VK_WHOLE_SIZE},
                                                       {ctx.buffer(data.indirect), 0, VK_WHOLE_SIZE}};
            VkWriteDescriptorSet writes[2]{};
            for (std::uint32_t i = 0; i < 2; ++i) {
                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = set;
                writes[i].dstBinding = i;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                writes[i].pBufferInfo = &buffers[i];
            }
            table.vkUpdateDescriptorSets(ctx.device().handle(), 2, writes, 0, nullptr);
            table.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
            table.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0, 1, &set, 0, nullptr);
            table.vkCmdPushConstants(cmd, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
            const std::uint32_t groups = (constants.resolution + kGroupSize - 1) / kGroupSize;
            table.vkCmdDispatch(cmd, groups, groups, 1);
        });
    draws.push_back({out.vertices, out.indirect, m_settings.model, m_settings.material});
}

} // namespace phox::vk
