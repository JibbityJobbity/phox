#include "phox/vulkan/passes/CopyPasses.hpp"

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

ImageHandle addBlitPass(RenderGraph &graph, ImageHandle source, ImageHandle destination) {
    struct Data {
        ImageHandle written;
    };
    return graph
        .addPass<Data>(
            "blit", PassType::Transfer,
            [&](PassBuilder &builder, Data &data) {
                builder.read(source, ImageUsage::TransferSrc);
                data.written = builder.write(destination, ImageUsage::TransferDst);
            },
            [source, destination](const Data &, phox::PassContext &context) {
                auto &ctx = PassContext::from(context);
                const Extent2D from = ctx.extent(source);
                const Extent2D to = ctx.extent(destination);
                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blit.srcOffsets[1] = {static_cast<int32_t>(from.width), static_cast<int32_t>(from.height), 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blit.dstOffsets[1] = {static_cast<int32_t>(to.width), static_cast<int32_t>(to.height), 1};
                ctx.device().table().vkCmdBlitImage(ctx.cmd(), ctx.image(source), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                                    ctx.image(destination), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                                    &blit, VK_FILTER_NEAREST);
            })
        .written;
}

} // namespace phox::vk
