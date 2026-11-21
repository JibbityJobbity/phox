#ifndef PHOX_FLUID_PERSISTENT_HPP
#define PHOX_FLUID_PERSISTENT_HPP

#include "phox/RenderGraph.hpp"
#include "phox/vulkan/FrameFeature.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace phox::vk {
class Buffer;
}

namespace fluid {

/// A device-local buffer that keeps its contents from frame to frame, imported into each frame's graph
/// with the async compute queue as its home: it arrives from and returns to the async queue's earlier work.
class PersistentBuffer {
public:
    PersistentBuffer();
    ~PersistentBuffer();
    PersistentBuffer(PersistentBuffer &&) noexcept;
    PersistentBuffer &operator=(PersistentBuffer &&) noexcept;

    /// @p usage: every phox::BufferUsage the buffer will see, as a mask. @p deviceAddress: shaders may read
    /// it through address() (still declare the read in the graph, or it gets no barrier).
    void create(phox::vk::Device &device, std::uint64_t size, phox::BufferUsageMask usage, std::string name,
                bool deviceAddress = false);
    void destroy();
    bool created() const { return m_buffer != nullptr; }
    std::uint64_t size() const { return m_size; }
    /// 0 unless created with a device address.
    std::uint64_t address() const;

    /// Imports it into @p context's graph. @p contents: what it holds matters (it was written before, in
    /// a StorageWrite state); else the graph may discard it. It leaves the graph in StorageWrite.
    phox::BufferHandle import(phox::vk::FrameFeatureContext &context, bool contents) const;

private:
    std::unique_ptr<phox::vk::Buffer> m_buffer;
    std::uint64_t m_size = 0;
    std::string m_name;
};

/// Adds an async compute pass running @p shader once per cell of a @p width x @p height grid, in 8 x 8
/// groups, with set 0 (SetLayout::ComputeStorage, eight buffers at most) binding @p reads (StorageRead) and then @p writes
/// (StorageWrite) in order, and @p push (at most 128 bytes) as push constants. Each write handle is
/// replaced by the version the pass produces.
void dispatchGrid(phox::vk::FrameFeatureContext &context, const std::string &name, const std::string &shader,
                  std::initializer_list<phox::BufferHandle> reads, std::initializer_list<phox::BufferHandle *> writes,
                  const void *push, std::size_t pushSize, std::uint32_t width, std::uint32_t height);

template <class Push>
void dispatchGrid(phox::vk::FrameFeatureContext &context, const std::string &name, const std::string &shader,
                  std::initializer_list<phox::BufferHandle> reads, std::initializer_list<phox::BufferHandle *> writes,
                  const Push &push, std::uint32_t width, std::uint32_t height) {
    static_assert(sizeof(Push) <= 128, "push constants are limited to 128 bytes");
    dispatchGrid(context, name, shader, reads, writes, &push, sizeof(Push), width, height);
}

/// Adds an async pass that zeroes @p buffer (replaced by the new version).
void clearBuffer(phox::vk::FrameFeatureContext &context, const std::string &name, phox::BufferHandle &buffer);

/// Push-constant form of a Splat (shaders/fluid_common.glsl).
struct GpuSplat {
    phox::vec4 positionForce;
    phox::vec4 radiusAmount;
};

} // namespace fluid

#endif // PHOX_FLUID_PERSISTENT_HPP
