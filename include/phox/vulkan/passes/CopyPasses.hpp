#ifndef PHOX_VULKAN_PASSES_COPY_PASSES_HPP
#define PHOX_VULKAN_PASSES_COPY_PASSES_HPP

#include "phox/RenderGraph.hpp"

namespace phox::vk {

/// Copies @p source (an RGBA8 colour image) into @p destination, a buffer at least width * height * 4
/// bytes. Returns the buffer's new version.
BufferHandle addReadbackPass(RenderGraph &graph, ImageHandle source, BufferHandle destination);

/// Scales @p source onto the whole of @p destination with a nearest-neighbour blit. Returns the
/// destination's new version.
ImageHandle addBlitPass(RenderGraph &graph, ImageHandle source, ImageHandle destination);

} // namespace phox::vk

#endif // PHOX_VULKAN_PASSES_COPY_PASSES_HPP
