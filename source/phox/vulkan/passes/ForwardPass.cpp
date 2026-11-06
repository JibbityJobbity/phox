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

PipelinePair vertexPipelinePair(PipelineCache &pipelines, Format color, Format depth, bool blend) {
    return {&pipelines.get(forwardPipelineDesc(color, depth, CullMode::None, blend)),
            &pipelines.get(forwardPipelineDesc(color, depth, CullMode::Back, blend))};
}

PipelinePair meshletPipelinePair(PipelineCache &pipelines, Format color, Format depth, MeshletPhase phase,
                                 bool blend = false) {
    return {&pipelines.get(forwardMeshletPipelineDesc(color, depth, CullMode::None, phase, blend)),
            &pipelines.get(forwardMeshletPipelineDesc(color, depth, CullMode::Back, phase, blend))};
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

// Records @p draws, whose materials start at entry @p firstMaterial of the frame's material table. Sets
// from 0 up are bound by the caller. @p visibilityOffsets: per draw, for occlusion culling; null otherwise.
void recordDraws(PassContext &ctx, const RenderView &view, const std::vector<DrawItem> &draws,
                 std::uint32_t firstMaterial, const MeshCache &meshes, const PipelinePair &pipeline,
                 const MeshletSettings &settings, const std::vector<std::uint32_t> *visibilityOffsets) {
    const auto &table = ctx.device().table();
    const VkCommandBuffer cmd = ctx.cmd();
    const VkPipelineLayout layout = pipeline[0]->layout();
    const Pipeline *bound = nullptr;
    for (std::size_t i = 0; i < draws.size(); ++i) {
        const DrawItem &draw = draws[i];
        const auto material = firstMaterial + static_cast<std::uint32_t>(i);
        const Pipeline *wanted = pipeline[draw.cullBackFaces ? 1 : 0];
        if (wanted != bound) {
            table.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, wanted->handle());
            bound = wanted;
        }
        const MeshCache::GpuMesh &mesh = meshes.get(*draw.mesh);
        if (meshes.meshlets()) {
            MeshletDrawPushConstants constants;
            constants.model = draw.model;
            constants.material = material;
            constants.cameraPosition = glm::inverse(draw.model) * vec4(view.cameraPosition, 1.0f);
            constants.vertices = mesh.vertices->deviceAddress();
            constants.meshlets = mesh.meshlets->deviceAddress();
            constants.meshletCount = mesh.meshletCount;
            if (settings.culling) {
                constants.flags = kMeshletFrustumCulling | (draw.cullBackFaces ? kMeshletConeCulling : 0u);
            }
            constants.lodThreshold = settings.lodErrorPixels;
            constants.visibilityOffset = visibilityOffsets != nullptr ? (*visibilityOffsets)[i] : 0u;
            table.vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT, 0,
                                     sizeof(constants), &constants);
            const std::uint32_t groups = (mesh.meshletCount + kMeshletTaskGroupSize - 1) / kMeshletTaskGroupSize;
            table.vkCmdDrawMeshTasksEXT(cmd, groups, 1, 1);
        } else {
            const VkBuffer vertexBuffer = mesh.vertices->handle();
            const VkDeviceSize offset = 0;
            table.vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer, &offset);
            table.vkCmdBindIndexBuffer(cmd, mesh.indices->handle(), 0, VK_INDEX_TYPE_UINT32);
            DrawPushConstants constants;
            constants.model = draw.model;
            constants.material = material;
            table.vkCmdPushConstants(cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(constants), &constants);
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

PipelineDesc forwardPipelineDesc(Format color, Format depth, CullMode cull, bool blend) {
    PipelineDesc desc;
    desc.name = blend ? "forward blend" : "forward";
    desc.vertexShader = "forward.vert";
    desc.fragmentShader = "forward.frag";
    desc.vertexLayout = VertexLayout::positionNormalTexcoordTangent();
    // Double-sided materials (e.g. the lone triangle) are drawn, and lit, from both sides.
    desc.raster.cull = cull;
    // Blended surfaces are tested against the opaque ones but do not hide each other.
    desc.depth = {true, !blend, CompareOp::Less};
    desc.blend = {blend ? BlendMode::Alpha : BlendMode::Opaque};
    desc.colorFormats = {color};
    desc.depthFormat = depth;
    desc.pushConstants = {{kVertexStage, 0, sizeof(DrawPushConstants)}};
    desc.setLayouts = {SetLayout::Frame, SetLayout::Bindless};
    return desc;
}

PipelineDesc forwardMeshletPipelineDesc(Format color, Format depth, CullMode cull, MeshletPhase phase, bool blend) {
    PipelineDesc desc;
    desc.meshShader = "forward.mesh";
    desc.fragmentShader = "forward.frag";
    desc.raster.cull = cull;
    desc.depth = {true, !blend, CompareOp::Less};
    desc.blend = {blend ? BlendMode::Alpha : BlendMode::Opaque};
    desc.colorFormats = {color};
    desc.depthFormat = depth;
    desc.pushConstants = {{kTaskStage | kMeshStage, 0, sizeof(MeshletDrawPushConstants)}};
    switch (phase) {
    case MeshletPhase::Single:
        desc.name = blend ? "forward meshlets blend" : "forward meshlets";
        desc.taskShader = "forward.task";
        desc.setLayouts = {SetLayout::Frame, SetLayout::Bindless};
        break;
    case MeshletPhase::Early:
        desc.name = "forward meshlets early";
        desc.taskShader = "forward_early.task";
        desc.setLayouts = {SetLayout::Frame, SetLayout::Bindless, SetLayout::MeshletVisibility};
        break;
    case MeshletPhase::Late:
        desc.name = "forward meshlets late";
        desc.taskShader = "forward_late.task";
        desc.setLayouts = {SetLayout::Frame, SetLayout::Bindless, SetLayout::MeshletVisibility, SetLayout::DepthPyramid};
        break;
    }
    return desc;
}

namespace {

// The opaque and alpha-masked draws (view.draws), in one pass or, with occlusion culling, two. @p keepDepth:
// a later pass tests against the depth.
ForwardPassOutput addOpaquePasses(RenderGraph &graph, const RenderView &view, const ForwardSets &sets,
                                  ImageHandle color, ImageHandle depth, MeshCache &meshes, PipelineCache &pipelines,
                                  const MeshletSettings &meshletSettings, const MeshletOcclusion *occlusion, bool keepDepth) {
    const Format colorFormat = graph.imageInfo(color).desc.format;
    const Format depthFormat = graph.imageInfo(depth).desc.format;
    const vec4 &c = view.clearColor;
    const StoreOp depthStore = keepDepth ? StoreOp::Store : StoreOp::DontCare;
    const VkDescriptorSet frameSet = sets.frame;
    const VkDescriptorSet textureSet = sets.textures;

    if (occlusion == nullptr) {
        const PipelinePair pipeline =
            meshes.meshlets() ? meshletPipelinePair(pipelines, colorFormat, depthFormat, MeshletPhase::Single)
                              : vertexPipelinePair(pipelines, colorFormat, depthFormat, false);
        return graph.addPass<ForwardPassOutput>(
            "forward", PassType::Graphics,
            [&](PassBuilder &builder, ForwardPassOutput &out) {
                out.color = builder.colorAttachment(color, LoadOp::Clear, StoreOp::Store, {c.r, c.g, c.b, c.a});
                // Depth is only needed while drawing, unless blended draws follow.
                out.depth = builder.depthAttachment(depth, LoadOp::Clear, depthStore, 1.0f);
            },
            [&view, frameSet, textureSet, &meshes, pipeline, meshletSettings](const ForwardPassOutput &,
                                                                               phox::PassContext &context) {
                auto &ctx = PassContext::from(context);
                bindSets(ctx, pipeline, {frameSet, textureSet});
                recordDraws(ctx, view, view.draws, 0, meshes, pipeline, meshletSettings, nullptr);
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
        [&view, frameSet, textureSet, &meshes, early, offsets, visibilitySet, meshletSettings](const EarlyData &data,
                                                                                                phox::PassContext &context) {
            auto &ctx = PassContext::from(context);
            bindSets(ctx, early, {frameSet, textureSet, visibilitySet(ctx, data.visibility)});
            recordDraws(ctx, view, view.draws, 0, meshes, early, {true, meshletSettings.lodErrorPixels}, offsets);
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
                data.out.depth = builder.depthAttachment(earlyOut.depth, LoadOp::Load, depthStore);
                data.pyramid = builder.read(pyramid, ImageUsage::Sampled);
                data.visibility = builder.write(earlyVisibility, BufferUsage::StorageWrite);
            },
            [&view, frameSet, textureSet, &meshes, late, offsets, visibilitySet, pyramidLayout,
             meshletSettings](const LateData &data, phox::PassContext &context) {
                auto &ctx = PassContext::from(context);
                const VkDescriptorSet pyramidSet = ctx.frame().descriptors().allocate(pyramidLayout);
                const VkDescriptorImageInfo info{VK_NULL_HANDLE, ctx.view(data.pyramid),
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                VkWriteDescriptorSet write = descriptorWrite(pyramidSet, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
                write.pImageInfo = &info;
                ctx.device().table().vkUpdateDescriptorSets(ctx.device().handle(), 1, &write, 0, nullptr);
                bindSets(ctx, late, {frameSet, textureSet, visibilitySet(ctx, data.visibility), pyramidSet});
                recordDraws(ctx, view, view.draws, 0, meshes, late, {true, meshletSettings.lodErrorPixels}, offsets);
            })
        .out;
}

} // namespace

ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, const ForwardSets &sets,
                                 ImageHandle color, ImageHandle depth, MeshCache &meshes, PipelineCache &pipelines,
                                 const MeshletSettings &meshletSettings, const MeshletOcclusion *occlusion) {
    const bool blended = !view.transparent.empty();
    const ForwardPassOutput opaque =
        addOpaquePasses(graph, view, sets, color, depth, meshes, pipelines, meshletSettings, occlusion, blended);
    if (!blended) {
        return opaque;
    }

    // Blended draws, farthest first, over the opaque image. Their materials follow view.draws' in the table.
    const Format colorFormat = graph.imageInfo(color).desc.format;
    const Format depthFormat = graph.imageInfo(depth).desc.format;
    const PipelinePair pipeline =
        meshes.meshlets() ? meshletPipelinePair(pipelines, colorFormat, depthFormat, MeshletPhase::Single, true)
                          : vertexPipelinePair(pipelines, colorFormat, depthFormat, true);
    const auto firstMaterial = static_cast<std::uint32_t>(view.draws.size());
    const VkDescriptorSet frameSet = sets.frame;
    const VkDescriptorSet textureSet = sets.textures;
    return graph.addPass<ForwardPassOutput>(
        "forward blend", PassType::Graphics,
        [&](PassBuilder &builder, ForwardPassOutput &out) {
            out.color = builder.colorAttachment(opaque.color, LoadOp::Load, StoreOp::Store);
            builder.depthReadOnly(opaque.depth);
            out.depth = opaque.depth;
        },
        [&view, frameSet, textureSet, &meshes, pipeline, meshletSettings, firstMaterial](const ForwardPassOutput &,
                                                                                          phox::PassContext &context) {
            auto &ctx = PassContext::from(context);
            bindSets(ctx, pipeline, {frameSet, textureSet});
            recordDraws(ctx, view, view.transparent, firstMaterial, meshes, pipeline, meshletSettings, nullptr);
        });
}

} // namespace phox::vk
