#include "phox/vulkan/passes/ForwardPass.hpp"

#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/GraphExecutor.hpp"
#include "phox/vulkan/MeshCache.hpp"
#include "phox/vulkan/PipelineCache.hpp"
#include "phox/vulkan/ShaderInterface.hpp"

namespace phox::vk {

PipelineDesc forwardPipelineDesc(Format color, Format depth) {
    PipelineDesc desc;
    desc.name = "forward";
    desc.vertexShader = "forward.vert";
    desc.fragmentShader = "forward.frag";
    desc.vertexLayout = VertexLayout::positionNormalTexcoord();
    desc.raster.cull = CullMode::None; // single-sided meshes (the triangle) are lit on both faces
    desc.depth = {true, true, CompareOp::Less};
    desc.colorFormats = {color};
    desc.depthFormat = depth;
    desc.pushConstants = {{kVertexStage | kFragmentStage, 0, sizeof(DrawPushConstants)}};
    desc.setLayouts = {SetLayout::Frame};
    return desc;
}

ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, VkDescriptorSet frameSet,
                                 ImageHandle color, ImageHandle depth, MeshCache &meshes, PipelineCache &pipelines) {
    const Pipeline &pipeline =
        pipelines.get(forwardPipelineDesc(graph.imageInfo(color).desc.format, graph.imageInfo(depth).desc.format));
    return graph.addPass<ForwardPassOutput>(
        "forward", PassType::Graphics,
        [&](PassBuilder &builder, ForwardPassOutput &out) {
            const vec4 &c = view.clearColor;
            out.color = builder.colorAttachment(color, LoadOp::Clear, StoreOp::Store, {c.r, c.g, c.b, c.a});
            // Depth is only needed while drawing.
            out.depth = builder.depthAttachment(depth, LoadOp::Clear, StoreOp::DontCare, 1.0f);
        },
        [&view, frameSet, &meshes, &pipeline](const ForwardPassOutput &, phox::PassContext &context) {
            if (view.draws.empty()) {
                return;
            }
            auto &ctx = PassContext::from(context);
            const auto &table = ctx.device().table();
            const VkCommandBuffer cmd = ctx.cmd();
            table.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.handle());
            table.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline.layout(), 0, 1, &frameSet, 0,
                                          nullptr);
            for (const DrawItem &draw : view.draws) {
                const MeshCache::GpuMesh &mesh = meshes.get(*draw.mesh);
                const VkBuffer vertexBuffer = mesh.vertices->handle();
                const VkDeviceSize offset = 0;
                table.vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
                table.vkCmdBindIndexBuffer(cmd, mesh.indices->handle(), 0, VK_INDEX_TYPE_UINT32);
                DrawPushConstants constants;
                constants.albedo = draw.material->albedo;
                constants.model = draw.model;
                table.vkCmdPushConstants(cmd, pipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                                         0, sizeof(constants), &constants);
                table.vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
            }
        });
}

} // namespace phox::vk
