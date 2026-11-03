#include "phox/vulkan/passes/ForwardPass.hpp"

#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/GraphExecutor.hpp"
#include "phox/vulkan/MeshCache.hpp"
#include "phox/vulkan/PipelineCache.hpp"
#include "phox/vulkan/ShaderInterface.hpp"

namespace phox::vk {

PipelineDesc forwardPipelineDesc(Format color, Format depth, CullMode cull) {
    PipelineDesc desc;
    desc.name = "forward";
    desc.vertexShader = "forward.vert";
    desc.fragmentShader = "forward.frag";
    desc.vertexLayout = VertexLayout::positionNormalTexcoord();
    // Double-sided materials (e.g. the lone triangle) are drawn, and lit, from both sides.
    desc.raster.cull = cull;
    desc.depth = {true, true, CompareOp::Less};
    desc.colorFormats = {color};
    desc.depthFormat = depth;
    desc.pushConstants = {{kVertexStage | kFragmentStage, 0, sizeof(DrawPushConstants)}};
    desc.setLayouts = {SetLayout::Frame};
    return desc;
}

PipelineDesc forwardMeshletPipelineDesc(Format color, Format depth, CullMode cull) {
    PipelineDesc desc;
    desc.name = "forward meshlets";
    desc.taskShader = "forward.task";
    desc.meshShader = "forward.mesh";
    desc.fragmentShader = "forward.frag";
    desc.raster.cull = cull;
    desc.depth = {true, true, CompareOp::Less};
    desc.colorFormats = {color};
    desc.depthFormat = depth;
    desc.pushConstants = {{kTaskStage | kMeshStage | kFragmentStage, 0, sizeof(MeshletDrawPushConstants)}};
    desc.setLayouts = {SetLayout::Frame};
    return desc;
}

ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, VkDescriptorSet frameSet,
                                 ImageHandle color, ImageHandle depth, MeshCache &meshes, PipelineCache &pipelines,
                                 bool meshletCulling) {
    const Format colorFormat = graph.imageInfo(color).desc.format;
    const Format depthFormat = graph.imageInfo(depth).desc.format;
    const auto pipelineDesc = meshes.meshlets() ? forwardMeshletPipelineDesc : forwardPipelineDesc;
    // [0]: double-sided, [1]: back faces culled. Both share a layout, so set 0 stays bound across them.
    const Pipeline *pipeline[2] = {&pipelines.get(pipelineDesc(colorFormat, depthFormat, CullMode::None)),
                                   &pipelines.get(pipelineDesc(colorFormat, depthFormat, CullMode::Back))};
    return graph.addPass<ForwardPassOutput>(
        "forward", PassType::Graphics,
        [&](PassBuilder &builder, ForwardPassOutput &out) {
            const vec4 &c = view.clearColor;
            out.color = builder.colorAttachment(color, LoadOp::Clear, StoreOp::Store, {c.r, c.g, c.b, c.a});
            // Depth is only needed while drawing.
            out.depth = builder.depthAttachment(depth, LoadOp::Clear, StoreOp::DontCare, 1.0f);
        },
        [&view, frameSet, &meshes, pipeline, meshletCulling](const ForwardPassOutput &, phox::PassContext &context) {
            if (view.draws.empty()) {
                return;
            }
            auto &ctx = PassContext::from(context);
            const auto &table = ctx.device().table();
            const VkCommandBuffer cmd = ctx.cmd();
            const VkPipelineLayout layout = pipeline[0]->layout();
            table.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &frameSet, 0, nullptr);
            const Pipeline *bound = nullptr;
            for (const DrawItem &draw : view.draws) {
                const Pipeline *wanted = pipeline[draw.cullBackFaces ? 1 : 0];
                if (wanted != bound) {
                    table.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wanted->handle());
                    bound = wanted;
                }
                const MeshCache::GpuMesh &mesh = meshes.get(*draw.mesh);
                if (meshes.meshlets()) {
                    MeshletDrawPushConstants constants;
                    constants.albedo = draw.material->albedo;
                    constants.model = draw.model;
                    constants.cameraPosition = glm::inverse(draw.model) * vec4(view.cameraPosition, 1.0f);
                    constants.vertices = mesh.vertices->deviceAddress();
                    constants.meshlets = mesh.meshlets->deviceAddress();
                    constants.meshletCount = mesh.meshletCount;
                    if (meshletCulling) {
                        constants.flags = kMeshletFrustumCulling | (draw.cullBackFaces ? kMeshletConeCulling : 0u);
                    }
                    table.vkCmdPushConstants(cmd, layout,
                                             VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT |
                                                 VK_SHADER_STAGE_FRAGMENT_BIT,
                                             0, sizeof(constants), &constants);
                    const std::uint32_t groups = (mesh.meshletCount + kMeshletTaskGroupSize - 1) / kMeshletTaskGroupSize;
                    table.vkCmdDrawMeshTasksEXT(cmd, groups, 1, 1);
                } else {
                    const VkBuffer vertexBuffer = mesh.vertices->handle();
                    const VkDeviceSize offset = 0;
                    table.vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
                    table.vkCmdBindIndexBuffer(cmd, mesh.indices->handle(), 0, VK_INDEX_TYPE_UINT32);
                    DrawPushConstants constants;
                    constants.albedo = draw.material->albedo;
                    constants.model = draw.model;
                    table.vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                                             sizeof(constants), &constants);
                    table.vkCmdDrawIndexed(cmd, mesh.indexCount, 1, 0, 0, 0);
                }
            }
        });
}

} // namespace phox::vk
