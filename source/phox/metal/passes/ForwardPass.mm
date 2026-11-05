#include "phox/metal/passes/ForwardPass.hpp"

#include "phox/metal/Buffer.hpp"
#include "phox/metal/GraphExecutor.hpp"
#include "phox/metal/MeshCache.hpp"
#include "phox/metal/PipelineCache.hpp"
#include "phox/metal/ShaderInterface.hpp"

#include <array>

namespace phox::mtl {

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

ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, const FrameUniforms &frame,
                                 ImageHandle color, ImageHandle depth, MeshCache &meshes, PipelineCache &pipelines) {
    const Format colorFormat = graph.imageInfo(color).desc.format;
    const Format depthFormat = graph.imageInfo(depth).desc.format;
    // [0]: double-sided, [1]: back faces culled.
    const std::array<const Pipeline *, 2> pipeline = {
        &pipelines.get(forwardPipelineDesc(colorFormat, depthFormat, CullMode::None)),
        &pipelines.get(forwardPipelineDesc(colorFormat, depthFormat, CullMode::Back))};
    return graph.addPass<ForwardPassOutput>(
        "forward", PassType::Graphics,
        [&](PassBuilder &builder, ForwardPassOutput &out) {
            const vec4 &c = view.clearColor;
            out.color = builder.colorAttachment(color, LoadOp::Clear, StoreOp::Store, {c.r, c.g, c.b, c.a});
            // Depth is only needed while drawing.
            out.depth = builder.depthAttachment(depth, LoadOp::Clear, StoreOp::DontCare, 1.0f);
        },
        [&view, frame, &meshes, pipeline](const ForwardPassOutput &, phox::PassContext &context) {
            if (view.draws.empty()) {
                return;
            }
            const id<MTLRenderCommandEncoder> encoder = PassContext::from(context).renderEncoder();
            // Small constants go inline in the command buffer: no upload memory or descriptors needed.
            [encoder setVertexBytes:&frame length:sizeof(frame) atIndex:kFrameBufferIndex];
            [encoder setFragmentBytes:&frame length:sizeof(frame) atIndex:kFrameBufferIndex];
            const Pipeline *bound = nullptr;
            for (const DrawItem &draw : view.draws) {
                const Pipeline *wanted = pipeline[draw.cullBackFaces ? 1 : 0];
                if (wanted != bound) {
                    wanted->bind(encoder);
                    bound = wanted;
                }
                const MeshCache::GpuMesh &mesh = meshes.get(*draw.mesh);
                DrawPushConstants constants;
                constants.albedo = draw.material->albedo;
                constants.model = draw.model;
                [encoder setVertexBuffer:mesh.vertices->handle() offset:0 atIndex:kVertexBufferIndex];
                [encoder setVertexBytes:&constants length:sizeof(constants) atIndex:kDrawBufferIndex];
                [encoder setFragmentBytes:&constants length:sizeof(constants) atIndex:kDrawBufferIndex];
                [encoder drawIndexedPrimitives:bound->primitive()
                                    indexCount:mesh.indexCount
                                     indexType:MTLIndexTypeUInt32
                                   indexBuffer:mesh.indices->handle()
                             indexBufferOffset:0];
            }
        });
}

} // namespace phox::mtl
