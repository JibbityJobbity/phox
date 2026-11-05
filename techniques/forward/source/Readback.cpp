#include "phox/forward/Readback.hpp"

#include <stdexcept>

namespace phox::forward {

namespace {

uint32_t bytesPerPixel(rhi::Format format) {
    switch (format) {
    case rhi::Format::RGBA8Unorm:
    case rhi::Format::RGBA8Srgb:
    case rhi::Format::BGRA8Unorm:
    case rhi::Format::R32Float: return 4;
    case rhi::Format::RGBA16Float: return 8;
    case rhi::Format::RGBA32Float: return 16;
    case rhi::Format::Undefined:
    case rhi::Format::D32Float:
    case rhi::Format::D24UnormS8Uint: break;
    }
    throw std::invalid_argument("Readback: unsupported texture format");
}

} // namespace

Readback::Out Readback::setup(graph::GraphBuilder &builder, const In &in) {
    const rhi::TextureDesc &desc = builder.describe(in.src);
    const uint64_t bytes = uint64_t{desc.width} * desc.height * bytesPerPixel(desc.format);

    const graph::GraphTexture src = in.src;
    const graph::GraphBuffer pixels = builder.createBuffer({bytes, rhi::BufferUsage{}, rhi::MemoryKind::Readback, "pixels"});
    builder.addPass(
        "readback",
        [=](graph::PassBuilder &pass) {
            pass.read(src, graph::TextureAccess::TransferSrc);
            pass.write(pixels, graph::BufferAccess::TransferDst);
            pass.sideEffect();
        },
        [=](graph::PassContext &context) { context.cmd.copyTextureToBuffer(context.texture(src), context.buffer(pixels)); });
    return {pixels};
}

} // namespace phox::forward
