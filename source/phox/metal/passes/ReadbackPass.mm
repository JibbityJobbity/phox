#include "phox/metal/passes/ReadbackPass.hpp"

#include "phox/metal/GraphExecutor.hpp"

namespace phox::mtl {

BufferHandle addReadbackPass(RenderGraph &graph, ImageHandle source, BufferHandle destination) {
    struct Data {
        BufferHandle written;
    };
    return graph
        .addPass<Data>(
            "readback", PassType::Transfer,
            [&](PassBuilder &builder, Data &data) {
                builder.read(source, ImageUsage::TransferSrc);
                data.written = builder.write(destination, BufferUsage::TransferDst);
            },
            [source, destination](const Data &, phox::PassContext &context) {
                auto &ctx = PassContext::from(context);
                const Extent2D extent = ctx.extent(source);
                const NSUInteger rowBytes = NSUInteger(extent.width) * 4;
                [ctx.blitEncoder() copyFromTexture:ctx.texture(source)
                                       sourceSlice:0
                                       sourceLevel:0
                                      sourceOrigin:MTLOriginMake(0, 0, 0)
                                        sourceSize:MTLSizeMake(extent.width, extent.height, 1)
                                          toBuffer:ctx.buffer(destination)
                                 destinationOffset:0
                            destinationBytesPerRow:rowBytes
                          destinationBytesPerImage:rowBytes * extent.height];
            })
        .written;
}

} // namespace phox::mtl
