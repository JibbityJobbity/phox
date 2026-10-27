#include "phox/vulkan/passes/ReadbackPass.hpp"

#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/GraphExecutor.hpp"

namespace phox::vk {

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
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {extent.width, extent.height, 1};
                ctx.device().table().vkCmdCopyImageToBuffer(ctx.cmd(), ctx.image(source),
                                                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                            ctx.buffer(destination), 1, &region);
            })
        .written;
}

} // namespace phox::vk
