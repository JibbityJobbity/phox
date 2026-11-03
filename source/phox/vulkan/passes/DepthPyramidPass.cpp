#include "phox/vulkan/passes/DepthPyramidPass.hpp"

#include "phox/Meshlets.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/GraphExecutor.hpp"
#include "phox/vulkan/PipelineCache.hpp"
#include "phox/vulkan/ShaderInterface.hpp"

#include <algorithm>
#include <stdexcept>

namespace phox::vk {

PipelineDesc depthPyramidPipelineDesc() {
    PipelineDesc desc;
    desc.name = "depth pyramid";
    desc.computeShader = "depthpyramid.comp";
    desc.pushConstants = {{kComputeStage, 0, sizeof(DepthPyramidPushConstants)}};
    desc.setLayouts = {SetLayout::DepthPyramidBuild};
    return desc;
}

ImageHandle addDepthPyramidPass(RenderGraph &graph, ImageHandle depth, PipelineCache &pipelines) {
    if (graph.imageInfo(depth).desc.format != Format::D32Float) {
        throw std::logic_error("the depth pyramid is built from D32Float depth");
    }
    const Extent2D extent = depthPyramidExtent(graph.outputExtent());
    const std::uint32_t levels = depthPyramidLevels(extent);
    const ImageHandle pyramid = graph.createImage("depth pyramid", ImageDesc{extent, Format::R32Float, levels});
    const Pipeline &pipeline = pipelines.get(depthPyramidPipelineDesc());
    const VkDescriptorSetLayout setLayout = pipelines.setLayout(SetLayout::DepthPyramidBuild);

    struct Data {
        ImageHandle depth;
        ImageHandle pyramid;
    };
    return graph
        .addPass<Data>(
            "depth pyramid", PassType::Compute,
            [&](PassBuilder &builder, Data &data) {
                data.depth = builder.read(depth, ImageUsage::Sampled);
                data.pyramid = builder.write(pyramid, ImageUsage::StorageWrite);
            },
            [extent, levels, &pipeline, setLayout](const Data &data, phox::PassContext &context) {
                auto &ctx = PassContext::from(context);
                const auto &table = ctx.device().table();
                const VkCommandBuffer cmd = ctx.cmd();
                table.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());

                // Each mip reads the one before, so the dispatches are ordered by barriers. The pyramid stays
                // in GENERAL throughout; the graph moves it on afterwards.
                VkMemoryBarrier2 barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
                barrier.srcStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                barrier.srcAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT;
                barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
                barrier.dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT;
                VkDependencyInfo dependency{};
                dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
                dependency.memoryBarrierCount = 1;
                dependency.pMemoryBarriers = &barrier;

                for (std::uint32_t level = 0; level < levels; ++level) {
                    if (level > 0) {
                        table.vkCmdPipelineBarrier2(cmd, &dependency);
                    }
                    // Mip 0 reads depth only; its source binding just needs a valid view of another mip.
                    const std::uint32_t sourceLevel = level > 0 ? level - 1 : std::min(1u, levels - 1);
                    const VkDescriptorImageInfo images[] = {
                        {VK_NULL_HANDLE, ctx.view(data.depth), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                        {VK_NULL_HANDLE, ctx.mipView(data.pyramid, sourceLevel), VK_IMAGE_LAYOUT_GENERAL},
                        {VK_NULL_HANDLE, ctx.mipView(data.pyramid, level), VK_IMAGE_LAYOUT_GENERAL},
                    };
                    const VkDescriptorSet set = ctx.frame().descriptors().allocate(setLayout);
                    VkWriteDescriptorSet writes[3]{};
                    for (std::uint32_t i = 0; i < 3; ++i) {
                        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                        writes[i].dstSet = set;
                        writes[i].dstBinding = i;
                        writes[i].descriptorCount = 1;
                        writes[i].descriptorType =
                            i == 0 ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                        writes[i].pImageInfo = &images[i];
                    }
                    table.vkUpdateDescriptorSets(ctx.device().handle(), 3, writes, 0, nullptr);
                    table.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), 0, 1, &set,
                                                  0, nullptr);
                    const DepthPyramidPushConstants constants{level};
                    table.vkCmdPushConstants(cmd, pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants),
                                             &constants);
                    const std::uint32_t width = std::max(extent.width >> level, 1u);
                    const std::uint32_t height = std::max(extent.height >> level, 1u);
                    table.vkCmdDispatch(cmd, (width + 7) / 8, (height + 7) / 8, 1);
                }
            })
        .pyramid;
}

} // namespace phox::vk
