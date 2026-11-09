#include "phox/vulkan/UploadArena.hpp"

#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace phox::vk {

namespace {

VkDeviceSize alignUp(VkDeviceSize value, VkDeviceSize alignment) { return (value + alignment - 1) / alignment * alignment; }

} // namespace

UploadArena::UploadArena(Device &device, VkDeviceSize blockSize) : m_device(device), m_blockSize(blockSize) {}

UploadArena::~UploadArena() = default;

UploadArena::Block UploadArena::makeBlock(VkDeviceSize size) {
    BufferCreateInfo info;
    info.size = size;
    info.usage = usageBit(BufferUsage::Vertex) | usageBit(BufferUsage::Index) | usageBit(BufferUsage::Uniform) |
                 usageBit(BufferUsage::StorageRead) | usageBit(BufferUsage::Indirect) |
                 usageBit(BufferUsage::TransferSrc);
    info.memory = MemoryLocation::Upload;
    info.name = "upload arena";
    info.deviceAddress = true; // shaders read the frame's scene arrays through addresses
    Block block;
    block.buffer = m_device.makeBuffer(info);
    if (block.buffer->mapped() == nullptr) {
        throw std::runtime_error("upload arena memory is not host visible");
    }
    return block;
}

VkDeviceSize UploadArena::uniformAlignment() const {
    return std::max<VkDeviceSize>(16, m_device.properties().limits.minUniformBufferOffsetAlignment);
}

UploadArena::Allocation UploadArena::allocate(VkDeviceSize size, VkDeviceSize alignment) {
    alignment = std::max<VkDeviceSize>(alignment, 4);
    if (m_blocks.empty() || alignUp(m_blocks.back().used, alignment) + size > m_blocks.back().buffer->size()) {
        m_blocks.push_back(makeBlock(std::max(m_blockSize, alignUp(size, 256))));
    }
    Block &block = m_blocks.back();
    const VkDeviceSize offset = alignUp(block.used, alignment);
    block.used = offset + size;
    return {block.buffer->handle(), offset, size, static_cast<std::uint8_t *>(block.buffer->mapped()) + offset,
            block.buffer->deviceAddress() + offset};
}

UploadArena::Allocation UploadArena::upload(const void *data, VkDeviceSize size, VkDeviceSize alignment) {
    Allocation allocation = allocate(size, alignment);
    std::memcpy(allocation.data, data, static_cast<std::size_t>(size));
    return allocation;
}

void UploadArena::flush() {
    for (Block &block : m_blocks) {
        if (block.used > 0) {
            block.buffer->flush(0, block.used);
        }
    }
}

void UploadArena::reset() {
    if (m_blocks.size() > 1) {
        // A frame overflowed the first block: replace them all with one block big enough for that frame.
        VkDeviceSize total = 0;
        for (const Block &block : m_blocks) total += block.buffer->size();
        m_blocks.clear();
        m_blockSize = std::max(m_blockSize, total);
    }
    for (Block &block : m_blocks) block.used = 0;
}

} // namespace phox::vk
