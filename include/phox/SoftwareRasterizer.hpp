#ifndef PHOX_SOFTWARE_RASTERIZER_HPP
#define PHOX_SOFTWARE_RASTERIZER_HPP

#include "phox/CpuImage.hpp"
#include "phox/RenderView.hpp"

namespace phox {

/// Renders a view on the CPU with the same projection, depth test and lighting as the GPU forward pass.
/// It exists so a frame can still be produced when no Vulkan device is available; it does no clipping
/// (triangles crossing the near plane are skipped) and follows the GPU only approximately on edges.
CpuImage rasterize(const RenderView &view);

} // namespace phox

#endif // PHOX_SOFTWARE_RASTERIZER_HPP
