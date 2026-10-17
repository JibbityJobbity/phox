#ifndef PHOX_RHI_HANDLES_HPP
#define PHOX_RHI_HANDLES_HPP

#include <compare>
#include <cstdint>

namespace phox::rhi {

template <class Tag> struct Handle {
    uint32_t index = UINT32_MAX;
    uint32_t generation = 0;
    bool valid() const { return index != UINT32_MAX; }
    auto operator<=>(const Handle &) const = default;
};
using BufferHandle = Handle<struct BufferTag>;
using TextureHandle = Handle<struct TextureTag>;
using ShaderHandle = Handle<struct ShaderTag>;
using PipelineHandle = Handle<struct PipelineTag>;
// must: Device pools bump generation on destroy; using a stale handle asserts in debug builds.

} // namespace phox::rhi

#endif // PHOX_RHI_HANDLES_HPP
