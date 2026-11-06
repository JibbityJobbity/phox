#include "phox/vulkan/MeshletVisibility.hpp"

#include "phox/Log.hpp"
#include "phox/RenderView.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/MeshCache.hpp"
#include "phox/vulkan/ShaderInterface.hpp"

#include <algorithm>

namespace phox::vk {

MeshletVisibility::MeshletVisibility(Device &device) : m_device(device) {}

MeshletVisibility::~MeshletVisibility() = default;

MeshletVisibility::Frame MeshletVisibility::prepare(const RenderView &view, const MeshCache &meshes,
                                                    FrameContext &frame) {
    const auto wordsFor = [&](const DrawItem &draw) {
        return (meshes.get(*draw.mesh).meshletCount + kMeshletTaskGroupSize - 1) / kMeshletTaskGroupSize;
    };
    const auto current = [&](const DrawItem &draw) {
        const auto it = m_ranges.find(draw.objectId);
        return it != m_ranges.end() && it->second.mesh == draw.mesh->id() &&
               it->second.meshVersion == draw.mesh->version() && it->second.words == wordsFor(draw);
    };

    Frame result;
    std::uint32_t needed = 0; // words for draws without a current range
    std::uint32_t total = 0;  // words for all draws
    for (const DrawItem &draw : view.draws) {
        total += wordsFor(draw);
        needed += current(draw) ? 0 : wordsFor(draw);
    }
    const std::uint64_t capacity = m_buffer ? m_buffer->size() / sizeof(std::uint32_t) : 0;
    // Also on the first frame with nothing to draw: the renderer imports the buffer regardless.
    if (!m_buffer || m_used + needed > capacity) {
        // Start over: every range is dropped and the bits cleared. Twice what this frame needs leaves room
        // for objects coming into view.
        m_ranges.clear();
        m_used = 0;
        const std::uint64_t words = std::max<std::uint64_t>({capacity, 2ull * total, kInitialWords});
        if (words > capacity) {
            if (m_buffer) {
                auto retired = std::shared_ptr<Buffer>(std::move(m_buffer));
                frame.deferred().push([retired]() mutable { retired.reset(); });
            }
            m_buffer = m_device.makeBuffer({words * sizeof(std::uint32_t),
                                            usageBit(BufferUsage::StorageRead) | usageBit(BufferUsage::StorageWrite) |
                                                usageBit(BufferUsage::TransferDst),
                                            MemoryLocation::DeviceLocal, "meshlet visibility"});
        }
        result.clear = true;
        log::debug("meshlet visibility: cleared, {} words for {} draw(s)", words, view.draws.size());
    }

    result.offsets.reserve(view.draws.size());
    for (const DrawItem &draw : view.draws) {
        if (!current(draw)) {
            m_ranges[draw.objectId] = {m_used, wordsFor(draw), draw.mesh->id(), draw.mesh->version()};
            m_used += wordsFor(draw);
        }
        result.offsets.push_back(m_ranges[draw.objectId].offset);
    }
    return result;
}

} // namespace phox::vk
