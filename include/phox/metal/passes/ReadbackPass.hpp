#ifndef PHOX_METAL_PASSES_READBACK_PASS_HPP
#define PHOX_METAL_PASSES_READBACK_PASS_HPP

#include "phox/RenderGraph.hpp"

namespace phox::mtl {

/// Copies @p source (an 8-bit RGBA image) into @p destination, a buffer of at least width * height * 4
/// bytes, tightly packed. Returns the buffer's new version.
BufferHandle addReadbackPass(RenderGraph &graph, ImageHandle source, BufferHandle destination);

} // namespace phox::mtl

#endif // PHOX_METAL_PASSES_READBACK_PASS_HPP
