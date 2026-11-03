#include "phox/vulkan/passes/ForwardPass.hpp"

#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/GraphExecutor.hpp"
#include "phox/vulkan/MeshCache.hpp"
#include "phox/vulkan/PipelineCache.hpp"
#include "phox/vulkan/ShaderInterface.hpp"
#include "phox/vulkan/passes/DepthPyramidPass.hpp"

#include <array>

namespace phox::vk {

namespace {

// [0]: double-sided, [1]: back faces culled. Both share a layout, so descriptor sets stay bound across them.
using PipelinePair = std::array<const Pipeline *, 2>;

PipelinePair pipelinePair(PipelineCache &pipelines, PipelineDesc (*describe)(Format, Format, CullMode), Format color,
                          Format depth) {
    return {&pipelines.get(describe(color, depth, CullMode::None)), &pipelines.get(describe(color, depth, CullMode::Back))};
}

PipelinePair meshletPipelinePair(PipelineCache &pipelines, Format color, Format depth, MeshletPhase phase) {
    return {&pipelines.get(forwardMeshletPipelineDesc(color, depth, CullMode::None, phase)),
            &pipelines.get(forwardMeshletPipelineDesc(color, depth, CullMode::Back, phase))};
}

VkWriteDescriptorSet descriptorWrite(VkDescriptorSet set, VkDescriptorType type) {
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = type;
    return write;
}

// Records every draw of @p view. Sets from 0 up are bound by the caller. @p visibilityOffsets: per draw,
// for occlusion culling; null otherwise.
void recordDraws(PassContext &ctx, const RenderView &view, const MeshCache &meshes, const PipelinePair &pipeline,
                 bool meshletCulling, const std::vector<std::uint32_t> *visibilityOffsets) {
    const auto &table = ctx.device().table();
    const VkCommandBuffer cmd = ctx.cmd();
    const VkPipelineLayout layout = pipeline[0]->layout();
    const Pipeline *bound = nullptr;
    for (std::size_t i = 0; i < view.draws.size(); ++i) {
        const DrawItem &draw = view.draws[i];
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
            constants.visibilityOffset = visibilityOffsets != nullptr ? (*visibilityOffsets)[i] : 0u;
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
}

// Binds set 0 (the frame) and the sets after it in @p sets.
void bindSets(PassContext &ctx, const PipelinePair &pipeline, std::initializer_list<VkDescriptorSet> sets) {
    ctx.device().table().vkCmdBindDescriptorSets(ctx.cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline[0]->layout(), 0,
                                                 static_cast<std::uint32_t>(sets.size()), sets.begin(), 0, nullptr);
}

} // namespace

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

PipelineDesc forwardMeshletPipelineDesc(Format color, Format depth, CullMode cull, MeshletPhase phase) {
    PipelineDesc desc;
    desc.meshShader = "forward.mesh";
    desc.fragmentShader = "forward.frag";
    desc.raster.cull = cull;
    desc.depth = {true, true, CompareOp::Less};
    desc.colorFormats = {color};
    desc.depthFormat = depth;
    desc.pushConstants = {{kTaskStage | kMeshStage | kFragmentStage, 0, sizeof(MeshletDrawPushConstants)}};
    switch (phase) {
    case MeshletPhase::Single:
        desc.name = "forward meshlets";
        desc.taskShader = "forward.task";
        desc.setLayouts = {SetLayout::Frame};
        break;
    case MeshletPhase::Early:
        desc.name = "forward meshlets early";
        desc.taskShader = "forward_early.task";
        desc.setLayouts = {SetLayout::Frame, SetLayout::MeshletVisibility};
        break;
    case MeshletPhase::Late:
        desc.name = "forward meshlets late";
        desc.taskShader = "forward_late.task";
        desc.setLayouts = {SetLayout::Frame, SetLayout::MeshletVisibility, SetLayout::DepthPyramid};
        break;
    }
    return desc;
}

ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, VkDescriptorSet frameSet,
                                 ImageHandle color, ImageHandle depth, MeshCache &meshes, PipelineCache &pipelines,
                                 bool meshletCulling, const MeshletOcclusion *occlusion) {
    const Format colorFormat = graph.imageInfo(color).desc.format;
    const Format depthFormat = graph.imageInfo(depth).desc.format;
    const vec4 &c = view.clearColor;

    if (occlusion == nullptr) {
        const PipelinePair pipeline =
            meshes.meshlets() ? meshletPipelinePair(pipelines, colorFormat, depthFormat, MeshletPhase::Single)
                              : pipelinePair(pipelines, forwardPipelineDesc, colorFormat, depthFormat);
        return graph.addPass<ForwardPassOutput>(
            "forward", PassType::Graphics,
            [&](PassBuilder &builder, ForwardPassOutput &out) {
                out.color = builder.colorAttachment(color, LoadOp::Clear, StoreOp::Store, {c.r, c.g, c.b, c.a});
                // Depth is only needed while drawing.
                out.depth = builder.depthAttachment(depth, LoadOp::Clear, StoreOp::DontCare, 1.0f);
            },
            [&view, frameSet, &meshes, pipeline, meshletCulling](const ForwardPassOutput &,
                                                                  phox::PassContext &context) {
                auto &ctx = PassContext::from(context);
                bindSets(ctx, pipeline, {frameSet});
                recordDraws(ctx, view, meshes, pipeline, meshletCulling, nullptr);
            });
    }

    // ---- Two-phase occlusion culling -----------------------------------------------------------------
    BufferHandle visibility = occlusion->visibility;
    const std::vector<std::uint32_t> *offsets = &occlusion->frame->offsets;
    if (occlusion->frame->clear) {
        visibility = graph
                         .addPass<BufferHandle>(
                             "clear visibility", PassType::Transfer,
                             [&](PassBuilder &builder, BufferHandle &out) {
                                 out = builder.write(visibility, BufferUsage::TransferDst);
                             },
                             [](const BufferHandle &buffer, phox::PassContext &context) {
                                 auto &ctx = PassContext::from(context);
                                 ctx.device().table().vkCmdFillBuffer(ctx.cmd(), ctx.buffer(buffer), 0, VK_WHOLE_SIZE, 0);
                             });
    }

    const VkDescriptorSetLayout visibilityLayout = pipelines.setLayout(SetLayout::MeshletVisibility);
    const auto visibilitySet = [visibilityLayout](PassContext &ctx, BufferHandle buffer) {
        const VkDescriptorSet set = ctx.frame().descriptors().allocate(visibilityLayout);
        const VkDescriptorBufferInfo info{ctx.buffer(buffer), 0, VK_WHOLE_SIZE};
        VkWriteDescriptorSet write = descriptorWrite(set, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
        write.pBufferInfo = &info;
        ctx.device().table().vkUpdateDescriptorSets(ctx.device().handle(), 1, &write, 0, nullptr);
        return set;
    };

    struct EarlyData {
        ForwardPassOutput out;
        BufferHandle visibility;
    };
    const PipelinePair early = meshletPipelinePair(pipelines, colorFormat, depthFormat, MeshletPhase::Early);
    const EarlyData &first = graph.addPass<EarlyData>(
        "forward early", PassType::Graphics,
        [&](PassBuilder &builder, EarlyData &data) {
            data.out.color = builder.colorAttachment(color, LoadOp::Clear, StoreOp::Store, {c.r, c.g, c.b, c.a});
            data.out.depth = builder.depthAttachment(depth, LoadOp::Clear, StoreOp::Store, 1.0f);
            data.visibility = builder.read(visibility, BufferUsage::StorageRead);
        },
        [&view, frameSet, &meshes, early, offsets, visibilitySet](const EarlyData &data, phox::PassContext &context) {
            auto &ctx = PassContext::from(context);
            bindSets(ctx, early, {frameSet, visibilitySet(ctx, data.visibility)});
            recordDraws(ctx, view, meshes, early, true, offsets);
        });

    const ImageHandle pyramid = addDepthPyramidPass(graph, first.out.depth, pipelines);

    struct LateData {
        ForwardPassOutput out;
        ImageHandle pyramid;
        BufferHandle visibility;
    };
    const PipelinePair late = meshletPipelinePair(pipelines, colorFormat, depthFormat, MeshletPhase::Late);
    const VkDescriptorSetLayout pyramidLayout = pipelines.setLayout(SetLayout::DepthPyramid);
    const ForwardPassOutput &earlyOut = first.out;
    const BufferHandle earlyVisibility = first.visibility;
    return graph
        .addPass<LateData>(
            "forward late", PassType::Graphics,
            [&](PassBuilder &builder, LateData &data) {
                data.out.color = builder.colorAttachment(earlyOut.color, LoadOp::Load, StoreOp::Store);
                data.out.depth = builder.depthAttachment(earlyOut.depth, LoadOp::Load, StoreOp::DontCare);
                data.pyramid = builder.read(pyramid, ImageUsage::Sampled);
                data.visibility = builder.write(earlyVisibility, BufferUsage::StorageWrite);
            },
            [&view, frameSet, &meshes, late, offsets, visibilitySet, pyramidLayout](const LateData &data,
                                                                                   phox::PassContext &context) {
                auto &ctx = PassContext::from(context);
                const VkDescriptorSet pyramidSet = ctx.frame().descriptors().allocate(pyramidLayout);
                const VkDescriptorImageInfo info{VK_NULL_HANDLE, ctx.view(data.pyramid),
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkWriteDescriptorSet write = descriptorWrite(pyramidSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
                write.pImageInfo = &info;
                ctx.device().table().vkUpdateDescriptorSets(ctx.device().handle(), 1, &write, 0, nullptr);
                bindSets(ctx, late, {frameSet, visibilitySet(ctx, data.visibility), pyramidSet});
                recordDraws(ctx, view, meshes, late, true, offsets);
            })
        .out;
}

} // namespace phox::vk
