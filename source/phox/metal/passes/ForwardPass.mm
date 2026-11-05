#include "phox/metal/passes/ForwardPass.hpp"

#include "phox/metal/Buffer.hpp"
#include "phox/metal/GraphExecutor.hpp"
#include "phox/metal/MeshCache.hpp"
#include "phox/metal/PipelineCache.hpp"
#include "phox/metal/ShaderInterface.hpp"

namespace phox::mtl {

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

ForwardPassOutput addForwardPass(RenderGraph &graph, const RenderView &view, const FrameUniforms &frame,
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
        [&view, frame, &meshes, &pipeline](const ForwardPassOutput &, phox::PassContext &context) {
            if (view.draws.empty()) {
                return;
            }
            const id<MTLRenderCommandEncoder> encoder = PassContext::from(context).renderEncoder();
            pipeline.bind(encoder);
            // Small constants go inline in the command buffer: no upload memory or descriptors needed.
            [encoder setVertexBytes:&frame length:sizeof(frame) atIndex:kFrameBufferIndex];
            [encoder setFragmentBytes:&frame length:sizeof(frame) atIndex:kFrameBufferIndex];
            for (const DrawItem &draw : view.draws) {
                const MeshCache::GpuMesh &mesh = meshes.get(*draw.mesh);
                DrawPushConstants constants;
                constants.albedo = draw.material->albedo;
                constants.model = draw.model;
                [encoder setVertexBuffer:mesh.vertices->handle() offset:0 atIndex:kVertexBufferIndex];
                [encoder setVertexBytes:&constants length:sizeof(constants) atIndex:kDrawBufferIndex];
                [encoder setFragmentBytes:&constants length:sizeof(constants) atIndex:kDrawBufferIndex];
                [encoder drawIndexedPrimitives:pipeline.primitive()
                                    indexCount:mesh.indexCount
                                     indexType:MTLIndexTypeUInt32
                                   indexBuffer:mesh.indices->handle()
                             indexBufferOffset:0];
            }
        });
}

} // namespace phox::mtl
