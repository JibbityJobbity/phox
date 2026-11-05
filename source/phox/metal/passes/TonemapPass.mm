#include "phox/metal/passes/TonemapPass.hpp"

#include "phox/metal/GraphExecutor.hpp"
#include "phox/metal/PipelineCache.hpp"
#include "phox/metal/ShaderInterface.hpp"

namespace phox::mtl {

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
            [hdr, &pipeline, constants](const Data &, phox::PassContext &context) {
                auto &ctx = PassContext::from(context);
                const id<MTLRenderCommandEncoder> encoder = ctx.renderEncoder();
                pipeline.bind(encoder);
                [encoder setFragmentTexture:ctx.texture(hdr) atIndex:kTextureIndex]; // read() needs no sampler
                [encoder setFragmentBytes:&constants length:sizeof(constants) atIndex:kDrawBufferIndex];
                [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
            })
        .written;
}

} // namespace phox::mtl
