#include "phox/vulkan/passes/TonemapPass.hpp"

#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/GraphExecutor.hpp"
#include "phox/vulkan/PipelineCache.hpp"
#include "phox/vulkan/ShaderInterface.hpp"

#include <stdexcept>

namespace phox::vk {

PipelineDesc tonemapPipelineDesc(Format target) {
    PipelineDesc desc;
    desc.name = "tonemap";
    desc.vertexShader = "fullscreen.vert"; // no vertex input: one oversized triangle
    desc.fragmentShader = "tonemap.frag";
    desc.colorFormats = {target};
    desc.pushConstants = {{kFragmentStage, 0, sizeof(TonemapPushConstants)}};
    desc.setLayouts = {SetLayout::Texture};
    return desc;
}

ImageHandle addTonemapPass(RenderGraph &graph, ImageHandle hdr, ImageHandle target, const ToneMapping &mapping,
                           PipelineCache &pipelines) {
    const Pipeline &pipeline = pipelines.get(tonemapPipelineDesc(graph.imageInfo(target).desc.format));
    const VkDescriptorSetLayout textureLayout = pipelines.setLayout(SetLayout::Texture);
    TonemapPushConstants constants;
    constants.exposure = mapping.exposure;
    constants.op = static_cast<std::uint32_t>(mapping.op);

    struct Data {
        ImageHandle written;
    };
    return graph
        .addPass<Data>(
            "tonemap", PassType::Graphics,
            [&](PassBuilder &builder, Data &data) {
                builder.read(hdr, ImageUsage::Sampled);
                data.written = builder.colorAttachment(target, LoadOp::DontCare); // every pixel is written
            },
            [hdr, &pipeline, textureLayout, constants](const Data &, phox::PassContext &context) {
                auto &ctx = PassContext::from(context);
                const auto &table = ctx.device().table();
                // The view of a transient is only known now, so the descriptor set is written here.
                const VkDescriptorSet set = ctx.frame().descriptors().allocate(textureLayout);
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageView = ctx.view(hdr); // the sampler is immutable in the layout
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = set;
                write.dstBinding = 0;
                write.descriptorCount = 1;
                write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                write.pImageInfo = &imageInfo;
                table.vkUpdateDescriptorSets(ctx.device().handle(), 1, &write, 0, nullptr);

                const VkCommandBuffer cmd = ctx.cmd();
                table.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
                table.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0, 1, &set, 0,
                                              nullptr);
                table.vkCmdPushConstants(cmd, pipeline.layout(), VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants),
                                         &constants);
                table.vkCmdDraw(cmd, 3, 1, 0, 0);
            })
        .written;
}

} // namespace phox::vk
