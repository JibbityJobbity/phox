#ifndef PHOX_VK_STATES_HPP
#define PHOX_VK_STATES_HPP

#include "VkCommon.hpp"

namespace phox::vulkan {

struct SyncState {
    VkPipelineStageFlags2 stage;
    VkAccessFlags2 access;
    VkImageLayout layout;
};

/// The one table mapping rhi::ResourceState to Vulkan stage / access / layout.
/// `asSource` is true for the `from` side of a barrier. Undefined as a source means "contents are
/// discarded; wait for everything that may have touched the resource". Undefined as a destination
/// is invalid and throws std::invalid_argument.
SyncState syncState(rhi::ResourceState state, bool asSource);

} // namespace phox::vulkan

#endif // PHOX_VK_STATES_HPP
