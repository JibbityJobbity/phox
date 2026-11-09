#ifndef PHOX_VULKAN_UPLOAD_ARENA_HPP
#define PHOX_VULKAN_UPLOAD_ARENA_HPP

#include "phox/vulkan/VulkanCommon.hpp"

#include <cstdint>
#include <memory>
#include <vector>

namespace phox::vk {

class Buffer;
class Device;

/// Host-visible scratch memory for one frame: uniform data, vertex streams, staging for uploads, and the
/// scene arrays GPU-driven passes read through device addresses.
/// Allocation is a pointer bump; everything is recycled at once by reset(), which may only be called
/// once the GPU has finished the frame that used it.
class UploadArena {
public:
    struct Allocation {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
        void *data = nullptr;
        VkDeviceAddress address = 0; ///< of the allocation's first byte
    };

    explicit UploadArena(Device &device, VkDeviceSize blockSize = 4u << 20);
    ~UploadArena();
    UploadArena(const UploadArena &) = delete;
    UploadArena &operator=(const UploadArena &) = delete;

    Allocation allocate(VkDeviceSize size, VkDeviceSize alignment = 16);
    Allocation upload(const void *data, VkDeviceSize size, VkDeviceSize alignment = 16);
    template <class T> Allocation uploadUniform(const T &value) { return upload(&value, sizeof(T), uniformAlignment()); }

    VkDeviceSize uniformAlignment() const;
    /// Makes this frame's writes visible to the GPU. Call before submitting.
    void flush();
    void reset();

private:
    struct Block {
        std::unique_ptr<Buffer> buffer;
        VkDeviceSize used = 0;
    };
    Block makeBlock(VkDeviceSize size);

    Device &m_device;
    VkDeviceSize m_blockSize;
    std::vector<Block> m_blocks;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_UPLOAD_ARENA_HPP
