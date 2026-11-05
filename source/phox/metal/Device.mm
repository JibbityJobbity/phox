#include "phox/metal/Device.hpp"

#include "phox/metal/Buffer.hpp"
#include "phox/metal/Conversions.hpp"
#include "phox/metal/Image.hpp"

#include <stdexcept>

namespace phox::mtl {

namespace {

// Upload and Readback memory is shared with the CPU: coherent, and on Apple silicon the same memory the
// GPU uses. Write-combining suits upload memory the CPU only writes.
MTLResourceOptions resourceOptions(MemoryLocation memory) {
    switch (memory) {
    case MemoryLocation::DeviceLocal: return MTLResourceStorageModePrivate;
    case MemoryLocation::Upload: return MTLResourceStorageModeShared | MTLResourceCPUCacheModeWriteCombined;
    case MemoryLocation::Readback: return MTLResourceStorageModeShared;
    }
    throw std::invalid_argument("unknown MemoryLocation");
}

NSString *toNSString(const std::string &text) { return [NSString stringWithUTF8String:text.c_str()]; }

} // namespace

Device::Device(id<MTLDevice> device) : m_device(device) {
    if (m_device == nil) {
        throw std::invalid_argument("mtl::Device needs an MTLDevice");
    }
    m_queue = [m_device newCommandQueue];
    if (m_queue == nil) {
        throw std::runtime_error("failed to create a Metal command queue");
    }
    m_queue.label = @"Phox";
}

Device::~Device() = default;

std::string Device::name() const { return m_device.name.UTF8String; }

std::unique_ptr<phox::Buffer> Device::createBuffer(const BufferCreateInfo &info) { return makeBuffer(info); }

std::unique_ptr<phox::Image> Device::createImage(const ImageCreateInfo &info) { return makeImage(info); }

void Device::waitIdle() {
    // A queue runs its command buffers in order, so an empty one completes after all earlier work.
    id<MTLCommandBuffer> marker = [m_queue commandBuffer];
    marker.label = @"wait idle";
    [marker commit];
    [marker waitUntilCompleted];
}

std::unique_ptr<Buffer> Device::makeBuffer(const BufferCreateInfo &info) {
    if (info.size == 0) {
        throw std::invalid_argument("buffer '" + info.name + "' has size 0");
    }
    id<MTLBuffer> buffer = [m_device newBufferWithLength:info.size options:resourceOptions(info.memory)];
    if (buffer == nil) {
        throw std::runtime_error("failed to create buffer '" + info.name + "' of " + std::to_string(info.size) +
                                 " bytes");
    }
    buffer.label = toNSString(info.name);
    return std::make_unique<Buffer>(buffer, info.size);
}

std::unique_ptr<Image> Device::makeImage(const ImageCreateInfo &info) {
    if (info.extent.width == 0 || info.extent.height == 0) {
        throw std::invalid_argument("image '" + info.name + "' has an empty extent");
    }
    MTLTextureDescriptor *descriptor = [MTLTextureDescriptor new];
    descriptor.textureType = info.arrayLayers > 1 ? MTLTextureType2DArray : MTLTextureType2D;
    descriptor.pixelFormat = toMtl(info.format);
    descriptor.width = info.extent.width;
    descriptor.height = info.extent.height;
    descriptor.mipmapLevelCount = info.mipLevels;
    descriptor.arrayLength = info.arrayLayers;
    descriptor.usage = textureUsage(info.usage);
    descriptor.storageMode = MTLStorageModePrivate;
    id<MTLTexture> texture = [m_device newTextureWithDescriptor:descriptor];
    if (texture == nil) {
        throw std::runtime_error("failed to create image '" + info.name + "'");
    }
    texture.label = toNSString(info.name);
    return std::make_unique<Image>(info, texture);
}

} // namespace phox::mtl
