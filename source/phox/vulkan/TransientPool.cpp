#include "phox/vulkan/TransientPool.hpp"

#include "phox/Log.hpp"
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

} // namespace

TransientPool::TransientPool(Device &device) : m_device(device) {}

TransientPool::~TransientPool() = default;

Image &TransientPool::acquire(const ImageCreateInfo &info, std::uint64_t frame) {
    for (Entry &entry : m_images) {
        if (!entry.inUse && compatible(entry.image->info(), info)) {
            entry.inUse = true;
            entry.lastUsed = frame;
            return *entry.image;
        }
    }
    Entry entry;
    entry.image = m_device.makeImage(info);
    entry.inUse = true;
    entry.lastUsed = frame;
    log::debug("transient pool: created '{}' {}x{} ({} image(s) pooled)", info.name, info.extent.width,
               info.extent.height, m_images.size() + 1);
    m_images.push_back(std::move(entry));
    return *m_images.back().image;
}

void TransientPool::release(const Image &image, std::uint64_t frame) {
    for (Entry &entry : m_images) {
        if (entry.image.get() == &image) {
            entry.inUse = false;
            entry.lastUsed = frame;
            return;
        }
    }
    throw std::logic_error("TransientPool::release: image is not from this pool");
}

void TransientPool::collect(std::uint64_t frame) {
    const auto stale = [frame](const Entry &entry) {
        return !entry.inUse && frame > entry.lastUsed + kEvictAfterFrames;
    };
    const auto removed = std::count_if(m_images.begin(), m_images.end(), stale);
    if (removed > 0) {
        m_images.erase(std::remove_if(m_images.begin(), m_images.end(), stale), m_images.end());
        log::debug("transient pool: evicted {} unused image(s)", removed);
    }
}

} // namespace phox::vk
