#include "phox/vulkan/TransientPool.hpp"

#include "phox/Log.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/Image.hpp"

#include <algorithm>
#include <stdexcept>

namespace phox::vk {

namespace {

bool compatible(const ImageCreateInfo &a, const ImageCreateInfo &b) {
    return a.extent == b.extent && a.format == b.format && a.mipLevels == b.mipLevels &&
           a.arrayLayers == b.arrayLayers && a.usage == b.usage;
}

bool compatible(const BufferCreateInfo &a, const BufferCreateInfo &b) {
    return a.size == b.size && a.usage == b.usage && a.memory == b.memory && a.deviceAddress == b.deviceAddress;
}

template <class Entries, class Info>
auto *findFree(Entries &entries, const Info &info) {
    for (auto &entry : entries) {
        if (!entry.inUse && compatible(entry.info, info)) return &entry;
    }
    return static_cast<typename Entries::value_type *>(nullptr);
}

template <class Entries, class T> void markReleased(Entries &entries, const T &resource, std::uint64_t frame) {
    for (auto &entry : entries) {
        if (entry.resource.get() == &resource) {
            entry.inUse = false;
            entry.lastUsed = frame;
            return;
        }
    }
    throw std::logic_error("TransientPool::release: resource is not from this pool");
}

template <class Entries> std::size_t evict(Entries &entries, std::uint64_t frame, std::uint64_t after) {
    const auto stale = [&](const auto &entry) { return !entry.inUse && frame > entry.lastUsed + after; };
    const auto count = static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(), stale));
    entries.erase(std::remove_if(entries.begin(), entries.end(), stale), entries.end());
    return count;
}

} // namespace

TransientPool::TransientPool(Device &device) : m_device(device) {}

TransientPool::~TransientPool() = default;

Image &TransientPool::acquire(const ImageCreateInfo &info, std::uint64_t frame) {
    auto *entry = findFree(m_images, info);
    if (entry == nullptr) {
        m_images.push_back({m_device.makeImage(info), info});
        entry = &m_images.back();
        log::debug("transient pool: created image '{}' {}x{} ({} pooled)", info.name, info.extent.width,
                   info.extent.height, m_images.size());
    }
    entry->inUse = true;
    entry->lastUsed = frame;
    return *entry->resource;
}

Buffer &TransientPool::acquire(const BufferCreateInfo &info, std::uint64_t frame) {
    auto *entry = findFree(m_buffers, info);
    if (entry == nullptr) {
        m_buffers.push_back({m_device.makeBuffer(info), info});
        entry = &m_buffers.back();
        log::debug("transient pool: created buffer '{}' of {} bytes ({} pooled)", info.name, info.size,
                   m_buffers.size());
    }
    entry->inUse = true;
    entry->lastUsed = frame;
    return *entry->resource;
}

void TransientPool::release(const Image &image, std::uint64_t frame) { markReleased(m_images, image, frame); }

void TransientPool::release(const Buffer &buffer, std::uint64_t frame) { markReleased(m_buffers, buffer, frame); }

void TransientPool::collect(std::uint64_t frame) {
    const std::size_t images = evict(m_images, frame, kEvictAfterFrames);
    const std::size_t buffers = evict(m_buffers, frame, kEvictAfterFrames);
    if (images + buffers > 0) {
        log::debug("transient pool: evicted {} image(s) and {} buffer(s)", images, buffers);
    }
}

} // namespace phox::vk
