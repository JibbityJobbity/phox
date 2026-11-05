#include "phox/metal/passes/ForwardPass.hpp"

#include "phox/metal/Buffer.hpp"
#include "phox/metal/GraphExecutor.hpp"
#include "phox/metal/MeshCache.hpp"
#include "phox/metal/PipelineCache.hpp"
#include "phox/metal/ShaderInterface.hpp"
#include "phox/metal/TextureCache.hpp"

#include <array>

namespace phox::mtl {

namespace {

// [0]: double-sided, [1]: back faces culled.
using PipelinePair = std::array<const Pipeline *, 2>;

PipelinePair pipelinePair(PipelineCache &pipelines, Format color, Format depth, bool blend) {
    return {&pipelines.get(forwardPipelineDesc(color, depth, CullMode::None, blend)),
            &pipelines.get(forwardPipelineDesc(color, depth, CullMode::Back, blend))};
}

// Draws @p draws with their materials: the factors as MaterialData, and each texture with its sampler (or
// the placeholder, with the MaterialData field left at kNoTexture).
void recordDraws(id<MTLRenderCommandEncoder> encoder, const FrameUniforms &frame, const std::vector<DrawItem> &draws,
                 const MeshCache &meshes, TextureCache &textures, const PipelinePair &pipeline) {
    // Small constants go inline in the command buffer: no upload memory or descriptors needed.
    [encoder setVertexBytes:&frame length:sizeof(frame) atIndex:kFrameBufferIndex];
    [encoder setFragmentBytes:&frame length:sizeof(frame) atIndex:kFrameBufferIndex];
    const Pipeline *bound = nullptr;
    for (const DrawItem &draw : draws) {
        const Pipeline *wanted = pipeline[draw.cullBackFaces ? 1 : 0];
        if (wanted != bound) {
            wanted->bind(encoder);
            bound = wanted;
        }
        const MeshCache::GpuMesh &mesh = meshes.get(*draw.mesh);
        DrawPushConstants constants;
        constants.model = draw.model;

        const Material &material = *draw.material;
        MaterialData data = materialData(material);
        std::array<std::uint32_t *, 5> slots{&data.baseColorTexture, &data.metallicRoughnessTexture, &data.normalTexture,
                                             &data.occlusionTexture, &data.emissiveTexture};
        const std::array<const Texture *, 5> used = material.textures();
        for (std::uint32_t i = 0; i < used.size(); ++i) {
            const std::uint32_t index = kMaterialTextureIndex + i;
            if (used[i] != nullptr) {
                *slots[i] = index;
                [encoder setFragmentTexture:textures.texture(*used[i]) atIndex:index];
                [encoder setFragmentSamplerState:textures.sampler(used[i]->sampler()) atIndex:index];
            } else {
                [encoder setFragmentTexture:textures.placeholder() atIndex:index];
                [encoder setFragmentSamplerState:textures.placeholderSampler() atIndex:index];
            }
        }

        [encoder setVertexBuffer:mesh.vertices->handle() offset:0 atIndex:kVertexBufferIndex];
        [encoder setVertexBytes:&constants length:sizeof(constants) atIndex:kDrawBufferIndex];
        [encoder setFragmentBytes:&data length:sizeof(data) atIndex:kMaterialBufferIndex];
        [encoder drawIndexedPrimitives:bound->primitive()
                            indexCount:mesh.indexCount
                             indexType:MTLIndexTypeUInt32
                           indexBuffer:mesh.indices->handle()
                     indexBufferOffset:0];
    }
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

ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, const FrameUniforms &frame,
                                 ImageHandle color, ImageHandle depth, MeshCache &meshes, TextureCache &textures,
                                 PipelineCache &pipelines) {
    const Format colorFormat = graph.imageInfo(color).desc.format;
    const Format depthFormat = graph.imageInfo(depth).desc.format;
    const bool blended = !view.transparent.empty();
    const PipelinePair opaquePipeline = pipelinePair(pipelines, colorFormat, depthFormat, false);
    const ForwardPassOutput &opaque = graph.addPass<ForwardPassOutput>(
        "forward", PassType::Graphics,
        [&](PassBuilder &builder, ForwardPassOutput &out) {
            const vec4 &c = view.clearColor;
            out.color = builder.colorAttachment(color, LoadOp::Clear, StoreOp::Store, {c.r, c.g, c.b, c.a});
            // Depth is only needed while drawing, unless blended draws follow.
            out.depth = builder.depthAttachment(depth, LoadOp::Clear, blended ? StoreOp::Store : StoreOp::DontCare, 1.0f);
        },
        [&view, frame, &meshes, &textures, opaquePipeline](const ForwardPassOutput &, phox::PassContext &context) {
            if (view.draws.empty()) {
                return;
            }
            recordDraws(PassContext::from(context).renderEncoder(), frame, view.draws, meshes, textures, opaquePipeline);
        });
    if (!blended) {
        return opaque;
    }

    // Blended draws, farthest first, over the opaque image.
    const PipelinePair blendPipeline = pipelinePair(pipelines, colorFormat, depthFormat, true);
    const ForwardPassOutput opaqueOut = opaque;
    return graph.addPass<ForwardPassOutput>(
        "forward blend", PassType::Graphics,
        [&](PassBuilder &builder, ForwardPassOutput &out) {
            out.color = builder.colorAttachment(opaqueOut.color, LoadOp::Load, StoreOp::Store);
            builder.depthReadOnly(opaqueOut.depth);
            out.depth = opaqueOut.depth;
        },
        [&view, frame, &meshes, &textures, blendPipeline](const ForwardPassOutput &, phox::PassContext &context) {
            recordDraws(PassContext::from(context).renderEncoder(), frame, view.transparent, meshes, textures,
                        blendPipeline);
        });
}

} // namespace phox::mtl
