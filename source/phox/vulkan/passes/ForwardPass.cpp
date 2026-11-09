#include "phox/vulkan/passes/ForwardPass.hpp"

#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/GraphExecutor.hpp"
#include "phox/vulkan/PipelineCache.hpp"
#include "phox/vulkan/ShaderInterface.hpp"
#include "phox/vulkan/passes/DepthPyramidPass.hpp"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace phox::vk {

namespace {

// [0]: double-sided, [1]: back faces culled; the buckets of the draw lists. Both share a layout, so
// descriptor sets stay bound across them.
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

// One cull pass's output: what to draw, the work per bucket, and the counts (and dispatch arguments).
struct DrawList {
    BufferHandle draws;
    BufferHandle work;
    BufferHandle counters;
};

DrawList createDrawList(RenderGraph &graph, const std::string &name, const GpuScene::Frame &scene, bool meshlets) {
    // A work item is (draw, meshlet group); a draw command is a VkDrawIndexedIndirectCommand.
    const std::uint64_t workEntry = meshlets ? 2 * sizeof(std::uint32_t) : sizeof(VkDrawIndexedIndirectCommand);
    return {graph.createBuffer(name + " draws", BufferDesc{scene.drawCapacity * sizeof(GpuDraw), true}),
            graph.createBuffer(name + " work", BufferDesc{2 * scene.workCapacity * workEntry, true}),
            graph.createBuffer(name + " counters", BufferDesc{sizeof(DrawCounters), true})};
}

void bindSets(PassContext &ctx, VkPipelineBindPoint bindPoint, VkPipelineLayout layout,
              std::initializer_list<VkDescriptorSet> sets) {
    ctx.device().table().vkCmdBindDescriptorSets(ctx.cmd(), bindPoint, layout, 0, static_cast<std::uint32_t>(sets.size()),
                                                 sets.begin(), 0, nullptr);
}

// A descriptor set binding @p pyramid as SetLayout::DepthPyramid.
VkDescriptorSet pyramidSet(PassContext &ctx, PipelineCache &pipelines, ImageHandle pyramid) {
    const VkDescriptorSet set = ctx.frame().descriptors().allocate(pipelines.setLayout(SetLayout::DepthPyramid));
    const VkDescriptorImageInfo info{VK_NULL_HANDLE, ctx.view(pyramid), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;
    ctx.device().table().vkUpdateDescriptorSets(ctx.device().handle(), 1, &write, 0, nullptr);
    return set;
}

// Zeroes the counters of @p lists (with @p clearWork, their work too), and with @p state the occlusion
// history and visibility bits.
void addClearPass(RenderGraph &graph, const std::vector<DrawList *> &lists, OcclusionState *state,
                  bool clearWork = false) {
    struct Data {
        std::vector<BufferHandle> buffers;
    };
    graph.addPass<Data>(
        "clear draw counters", PassType::Transfer,
        [&](PassBuilder &builder, Data &data) {
            for (DrawList *list : lists) {
                list->counters = builder.write(list->counters, BufferUsage::TransferDst);
                data.buffers.push_back(list->counters);
                if (clearWork) {
                    list->work = builder.write(list->work, BufferUsage::TransferDst);
                    data.buffers.push_back(list->work);
                }
            }
            if (state != nullptr) {
                state->history = builder.write(state->history, BufferUsage::TransferDst);
                state->visibility = builder.write(state->visibility, BufferUsage::TransferDst);
                data.buffers.push_back(state->history);
                data.buffers.push_back(state->visibility);
            }
        },
        [](const Data &data, phox::PassContext &context) {
            auto &ctx = PassContext::from(context);
            for (BufferHandle buffer : data.buffers) {
                ctx.device().table().vkCmdFillBuffer(ctx.cmd(), ctx.buffer(buffer), 0, VK_WHOLE_SIZE, 0);
            }
        });
}

// The instance cull pass: appends what @p variant keeps to @p list.
void addCullPass(RenderGraph &graph, const std::string &name, CullVariant variant, const GpuScene::Frame &scene,
                 const ForwardSets &sets, bool frustumCulling, DrawList &list, BufferHandle *history,
                 ImageHandle pyramid, PipelineCache &pipelines) {
    const Pipeline &pipeline = pipelines.get(cullPipelineDesc(variant));
    const bool usesHistory = variant == CullVariant::Early || variant == CullVariant::Late;
    struct Data {
        DrawList list;
        BufferHandle history;
        ImageHandle pyramid;
    };
    const Data &data = graph.addPass<Data>(
        name, PassType::Compute,
        [&](PassBuilder &builder, Data &out) {
            out.list.draws = builder.write(list.draws, BufferUsage::StorageWrite);
            out.list.work = builder.write(list.work, BufferUsage::StorageWrite);
            out.list.counters = builder.write(list.counters, BufferUsage::StorageWrite);
            if (variant == CullVariant::Early) {
                out.history = builder.read(*history, BufferUsage::StorageRead);
            } else if (variant == CullVariant::Late) {
                out.history = builder.write(*history, BufferUsage::StorageWrite);
                out.pyramid = builder.read(pyramid, ImageUsage::Sampled);
            }
        },
        [&scene, &pipeline, &pipelines, frameSet = sets.frame, variant, usesHistory,
         frustumCulling](const Data &in, phox::PassContext &context) {
            auto &ctx = PassContext::from(context);
            const auto &table = ctx.device().table();
            table.vkCmdBindPipeline(ctx.cmd(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
            if (variant == CullVariant::Late) {
                bindSets(ctx, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(),
                         {frameSet, pyramidSet(ctx, pipelines, in.pyramid)});
            } else {
                bindSets(ctx, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.layout(), {frameSet});
            }
            CullPushConstants constants;
            constants.instances = scene.instances;
            constants.meshes = scene.meshes;
            constants.draws = ctx.bufferAddress(in.list.draws);
            constants.work = ctx.bufferAddress(in.list.work);
            constants.counters = ctx.bufferAddress(in.list.counters);
            if (usesHistory) {
                constants.history = ctx.bufferAddress(in.history);
            }
            constants.instanceCount = scene.instanceCount;
            constants.workCapacity = scene.workCapacity;
            constants.flags = frustumCulling ? kCullFrustum : 0u;
            table.vkCmdPushConstants(ctx.cmd(), pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants),
                                     &constants);
            table.vkCmdDispatch(ctx.cmd(), (scene.instanceCount + 63) / 64, 1, 1);
        });
    list = data.list;
    if (usesHistory) {
        *history = data.history;
    }
}

// Turns @p list's work counts into task shader dispatch arguments.
void addDrawArgsPass(RenderGraph &graph, const std::string &name, DrawList &list, PipelineCache &pipelines) {
    const Pipeline &pipeline = pipelines.get(drawArgsPipelineDesc());
    list.counters = graph.addPass<BufferHandle>(
        name, PassType::Compute,
        [&](PassBuilder &builder, BufferHandle &out) { out = builder.write(list.counters, BufferUsage::StorageWrite); },
        [&pipeline](const BufferHandle &counters, phox::PassContext &context) {
            auto &ctx = PassContext::from(context);
            const auto &table = ctx.device().table();
            table.vkCmdBindPipeline(ctx.cmd(), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline.handle());
            ArgsPushConstants constants;
            constants.counters = ctx.bufferAddress(counters);
            constants.maxGroupsX = ctx.device().meshShaderProperties().maxTaskWorkGroupCount[0];
            table.vkCmdPushConstants(ctx.cmd(), pipeline.layout(), VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants),
                                     &constants);
            table.vkCmdDispatch(ctx.cmd(), 1, 1, 1);
        });
}

// Records the indirect draws of one list, one per bucket: a task shader dispatch whose arguments the
// args pass wrote (meshlet path), or the cull pass's indexed draws with its count (vertex path; without
// @p drawCount, every command, the unused ones cleared to zero).
void drawList(PassContext &ctx, const PipelinePair &pipeline, bool meshlets, const MeshletDrawPushConstants &base,
              VkBuffer work, VkBuffer counters, std::uint32_t workCapacity, VkBuffer indexPool, bool drawCount) {
    const auto &table = ctx.device().table();
    constexpr std::uint32_t kStride = sizeof(VkDrawIndexedIndirectCommand);
    if (!meshlets) {
        table.vkCmdBindIndexBuffer(ctx.cmd(), indexPool, 0, VK_INDEX_TYPE_UINT32);
    }
    for (std::uint32_t bucket = 0; bucket < 2; ++bucket) {
        table.vkCmdBindPipeline(ctx.cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline[bucket]->handle());
        if (meshlets) {
            MeshletDrawPushConstants constants = base;
            constants.bucket = bucket;
            table.vkCmdPushConstants(ctx.cmd(), pipeline[bucket]->layout(),
                                     VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT, 0, sizeof(constants),
                                     &constants);
            table.vkCmdDrawMeshTasksIndirectEXT(ctx.cmd(), counters, kDrawCountersArgsOffset + bucket * 16u, 1, 16);
        } else {
            const VertexDrawPushConstants constants{base.draws, 0};
            table.vkCmdPushConstants(ctx.cmd(), pipeline[bucket]->layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                                     sizeof(constants), &constants);
            const VkDeviceSize offset = VkDeviceSize{bucket} * workCapacity * kStride;
            if (drawCount) {
                table.vkCmdDrawIndexedIndirectCount(ctx.cmd(), work, offset, counters, kDrawCountersWorkOffset + bucket * 4u,
                                                    workCapacity, kStride);
            } else {
                // In calls of at most maxDrawIndirectCount, which may be as low as 2^16 - 1.
                const std::uint32_t limit = ctx.device().properties().limits.maxDrawIndirectCount;
                for (std::uint32_t first = 0; first < workCapacity; first += limit) {
                    table.vkCmdDrawIndexedIndirect(ctx.cmd(), work, offset + VkDeviceSize{first} * kStride,
                                                   std::min(limit, workCapacity - first), kStride);
                }
            }
        }
    }
}

} // namespace

PipelineDesc forwardPipelineDesc(Format color, Format depth, CullMode cull, bool blend) {
    PipelineDesc desc;
    desc.name = blend ? "forward blend" : "forward";
    desc.vertexShader = "forward.vert"; // fetches its own vertices: no vertex input
    desc.fragmentShader = "forward.frag";
    // Double-sided materials (e.g. the lone triangle) are drawn, and lit, from both sides.
    desc.raster.cull = cull;
    // Blended surfaces are tested against the opaque ones but do not hide each other.
    desc.depth = {true, !blend, CompareOp::Less};
    desc.blend = {blend ? BlendMode::Alpha : BlendMode::Opaque};
    desc.colorFormats = {color};
    desc.depthFormat = depth;
    desc.pushConstants = {{kVertexStage, 0, sizeof(VertexDrawPushConstants)}};
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
    desc.setLayouts = {SetLayout::Frame, SetLayout::Bindless};
    switch (phase) {
    case MeshletPhase::Single:
        desc.name = blend ? "forward meshlets blend" : "forward meshlets";
        desc.taskShader = "forward.task";
        break;
    case MeshletPhase::Early:
        desc.name = "forward meshlets early";
        desc.taskShader = "forward_early.task";
        break;
    case MeshletPhase::Late:
        desc.name = "forward meshlets late";
        desc.taskShader = "forward_late.task";
        desc.setLayouts.push_back(SetLayout::DepthPyramid);
        break;
    }
    return desc;
}

PipelineDesc cullPipelineDesc(CullVariant variant) {
    PipelineDesc desc;
    desc.pushConstants = {{kComputeStage, 0, sizeof(CullPushConstants)}};
    desc.setLayouts = {SetLayout::Frame};
    switch (variant) {
    case CullVariant::Meshlets:
        desc.name = "cull";
        desc.computeShader = "cull.comp";
        break;
    case CullVariant::Early:
        desc.name = "cull early";
        desc.computeShader = "cull_early.comp";
        break;
    case CullVariant::Late:
        desc.name = "cull late";
        desc.computeShader = "cull_late.comp";
        desc.setLayouts.push_back(SetLayout::DepthPyramid);
        break;
    case CullVariant::Vertex:
        desc.name = "cull vertex";
        desc.computeShader = "cull_vertex.comp";
        break;
    }
    return desc;
}

PipelineDesc drawArgsPipelineDesc() {
    PipelineDesc desc;
    desc.name = "draw args";
    desc.computeShader = "draw_args.comp";
    desc.pushConstants = {{kComputeStage, 0, sizeof(ArgsPushConstants)}};
    return desc;
}

ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, const ForwardSets &sets,
                                 const GpuScene::Frame &scene, ImageHandle color, ImageHandle depth, bool meshlets,
                                 PipelineCache &pipelines, const MeshletSettings &settings,
                                 const OcclusionState *occlusion) {
    const Format colorFormat = graph.imageInfo(color).desc.format;
    const Format depthFormat = graph.imageInfo(depth).desc.format;
    const vec4 &c = view.clearColor;
    const bool blended = !scene.blendRuns.empty();
    MeshletDrawPushConstants meshletBase;
    meshletBase.workCapacity = scene.workCapacity;
    meshletBase.flags = settings.culling ? kMeshletFrustumCulling | kMeshletConeCulling : 0u;
    meshletBase.lodThreshold = settings.lodErrorPixels;
    const VkDescriptorSet frameSet = sets.frame;
    const VkDescriptorSet textureSet = sets.textures;

    // One pass drawing @p list. @p first clears the attachments; otherwise it draws over them.
    struct ForwardData {
        ForwardPassOutput out;
        DrawList list;
        BufferHandle visibility;
        ImageHandle pyramid;
    };
    const auto addDrawPass = [&](const std::string &name, const DrawList &list, const PipelinePair &pipeline,
                                 bool first, ImageHandle colorIn, ImageHandle depthIn, MeshletPhase phase,
                                 BufferHandle visibility, ImageHandle pyramid, bool keepDepth) -> const ForwardData & {
        return graph.addPass<ForwardData>(
            name, PassType::Graphics,
            [&](PassBuilder &builder, ForwardData &data) {
                data.out.color = first ? builder.colorAttachment(colorIn, LoadOp::Clear, StoreOp::Store, {c.r, c.g, c.b, c.a})
                                       : builder.colorAttachment(colorIn, LoadOp::Load, StoreOp::Store);
                // Depth is only needed while drawing, unless a later pass tests against it.
                data.out.depth = builder.depthAttachment(depthIn, first ? LoadOp::Clear : LoadOp::Load,
                                                         keepDepth ? StoreOp::Store : StoreOp::DontCare, 1.0f);
                data.list.counters = builder.read(list.counters, BufferUsage::Indirect);
                data.list.draws = builder.read(list.draws, BufferUsage::StorageRead);
                // Work items are read by the task shader; draw commands by the draw itself.
                data.list.work = builder.read(list.work, meshlets ? BufferUsage::StorageRead : BufferUsage::Indirect);
                if (phase == MeshletPhase::Early) {
                    data.visibility = builder.read(visibility, BufferUsage::StorageRead);
                } else if (phase == MeshletPhase::Late) {
                    data.visibility = builder.write(visibility, BufferUsage::StorageWrite);
                    data.pyramid = builder.read(pyramid, ImageUsage::Sampled);
                }
            },
            [pipeline, meshlets, meshletBase, frameSet, textureSet, phase, &pipelines, workCapacity = scene.workCapacity,
             indexPool = scene.indexPool, drawCount = scene.drawCount](const ForwardData &data, phox::PassContext &context) {
                auto &ctx = PassContext::from(context);
                if (phase == MeshletPhase::Late) {
                    bindSets(ctx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline[0]->layout(),
                             {frameSet, textureSet, pyramidSet(ctx, pipelines, data.pyramid)});
                } else {
                    bindSets(ctx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline[0]->layout(), {frameSet, textureSet});
                }
                MeshletDrawPushConstants constants = meshletBase;
                constants.draws = ctx.bufferAddress(data.list.draws);
                constants.work = ctx.bufferAddress(data.list.work);
                constants.counters = ctx.bufferAddress(data.list.counters);
                if (phase == MeshletPhase::Early || phase == MeshletPhase::Late) {
                    constants.visibility = ctx.bufferAddress(data.visibility);
                }
                drawList(ctx, pipeline, meshlets, constants, ctx.buffer(data.list.work), ctx.buffer(data.list.counters),
                         workCapacity, indexPool, drawCount);
            });
    };

    ForwardPassOutput result;
    if (meshlets && occlusion != nullptr) {
        // ---- Two-phase occlusion culling ------------------------------------------------------------
        OcclusionState state = *occlusion;
        DrawList early = createDrawList(graph, "early", scene, true);
        DrawList late = createDrawList(graph, "late", scene, true);
        addClearPass(graph, {&early, &late}, scene.clearState ? &state : nullptr);
        addCullPass(graph, "cull early", CullVariant::Early, scene, sets, settings.culling, early, &state.history, {},
                    pipelines);
        addDrawArgsPass(graph, "draw args early", early, pipelines);
        const ForwardData &first =
            addDrawPass("forward early", early, meshletPipelinePair(pipelines, colorFormat, depthFormat, MeshletPhase::Early),
                        true, color, depth, MeshletPhase::Early, state.visibility, {}, true);
        const ForwardPassOutput earlyOut = first.out;
        const BufferHandle visibility = first.visibility;
        const ImageHandle pyramid = addDepthPyramidPass(graph, earlyOut.depth, pipelines);
        addCullPass(graph, "cull late", CullVariant::Late, scene, sets, settings.culling, late, &state.history, pyramid,
                    pipelines);
        addDrawArgsPass(graph, "draw args late", late, pipelines);
        result = addDrawPass("forward late", late, meshletPipelinePair(pipelines, colorFormat, depthFormat, MeshletPhase::Late),
                             false, earlyOut.color, earlyOut.depth, MeshletPhase::Late, visibility, pyramid, blended)
                     .out;
    } else {
        DrawList list = createDrawList(graph, "frame", scene, meshlets);
        addClearPass(graph, {&list}, nullptr, !meshlets && !scene.drawCount);
        addCullPass(graph, "cull", meshlets ? CullVariant::Meshlets : CullVariant::Vertex, scene, sets, settings.culling,
                    list, nullptr, {}, pipelines);
        if (meshlets) {
            addDrawArgsPass(graph, "draw args", list, pipelines);
        }
        const PipelinePair pipeline = meshlets ? meshletPipelinePair(pipelines, colorFormat, depthFormat, MeshletPhase::Single)
                                               : vertexPipelinePair(pipelines, colorFormat, depthFormat, false);
        result = addDrawPass("forward", list, pipeline, true, color, depth, MeshletPhase::Single, {}, {}, blended).out;
    }
    if (!blended) {
        return result;
    }

    // Blended draws, farthest first, over the opaque image, in the runs GpuScene wrote.
    const PipelinePair pipeline = meshlets ? meshletPipelinePair(pipelines, colorFormat, depthFormat, MeshletPhase::Single, true)
                                           : vertexPipelinePair(pipelines, colorFormat, depthFormat, true);
    const ForwardPassOutput opaque = result;
    return graph.addPass<ForwardPassOutput>(
        "forward blend", PassType::Graphics,
        [&](PassBuilder &builder, ForwardPassOutput &out) {
            out.color = builder.colorAttachment(opaque.color, LoadOp::Load, StoreOp::Store);
            builder.depthReadOnly(opaque.depth);
            out.depth = opaque.depth;
        },
        [&scene, pipeline, meshlets, meshletBase, frameSet, textureSet](const ForwardPassOutput &,
                                                                         phox::PassContext &context) {
            auto &ctx = PassContext::from(context);
            const auto &table = ctx.device().table();
            bindSets(ctx, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline[0]->layout(), {frameSet, textureSet});
            if (!meshlets) {
                table.vkCmdBindIndexBuffer(ctx.cmd(), scene.indexPool, 0, VK_INDEX_TYPE_UINT32);
            }
            for (const GpuScene::BlendRun &run : scene.blendRuns) {
                const Pipeline &runPipeline = *pipeline[run.cullBackFaces ? 1 : 0];
                table.vkCmdBindPipeline(ctx.cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, runPipeline.handle());
                if (meshlets) {
                    MeshletDrawPushConstants constants = meshletBase;
                    constants.draws = scene.blendDraws;
                    constants.work = run.work;
                    constants.counters = run.counters;
                    table.vkCmdPushConstants(ctx.cmd(), runPipeline.layout(),
                                             VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT, 0,
                                             sizeof(constants), &constants);
                    table.vkCmdDrawMeshTasksIndirectEXT(ctx.cmd(), run.buffer, run.argsOffset, 1, 16);
                } else {
                    const VertexDrawPushConstants constants{scene.blendDraws, 0};
                    table.vkCmdPushConstants(ctx.cmd(), runPipeline.layout(), VK_SHADER_STAGE_VERTEX_BIT, 0,
                                             sizeof(constants), &constants);
                    table.vkCmdDrawIndexedIndirect(ctx.cmd(), run.buffer, run.argsOffset, run.drawCount,
                                                   sizeof(VkDrawIndexedIndirectCommand));
                }
            }
        });
}

} // namespace phox::vk
