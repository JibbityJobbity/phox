#define VMA_IMPLEMENTATION

#include "phox/vulkan/Device.hpp"

#include "phox/Log.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Conversions.hpp"
#include "phox/vulkan/Image.hpp"

#include <stdexcept>
#include <vector>

namespace phox::vk {

namespace {

struct QueueFamilies {
    std::uint32_t graphics = UINT32_MAX;
    std::uint32_t present = UINT32_MAX;
};

// Prefers one family that can both draw and present, so images never change queue ownership.
QueueFamilies pickQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, families.data());

    QueueFamilies result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const bool graphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        VkBool32 present = VK_FALSE;
        if (surface != VK_NULL_HANDLE) {
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &present);
        }
        if (graphics && (present == VK_TRUE || surface == VK_NULL_HANDLE)) {
            return {i, i};
        }
        if (graphics && result.graphics == UINT32_MAX) result.graphics = i;
        if (present == VK_TRUE && result.present == UINT32_MAX) result.present = i;
    }
    if (result.graphics == UINT32_MAX) {
        throw std::runtime_error("the device has no graphics queue");
    }
    if (surface != VK_NULL_HANDLE && result.present == UINT32_MAX) {
        throw std::runtime_error("the device cannot present to the window surface");
    }
    return result;
}

VmaAllocationCreateInfo allocationInfoFor(MemoryLocation memory) {
    VmaAllocationCreateInfo info{};
    switch (memory) {
    case MemoryLocation::DeviceLocal:
        info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case MemoryLocation::Upload:
        info.usage = VMA_MEMORY_USAGE_AUTO;
        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case MemoryLocation::Readback:
        info.usage = VMA_MEMORY_USAGE_AUTO;
        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }
    return info;
}

} // namespace

Device::Device(VkInstance instance, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, bool objectNames)
    : m_instance(instance), m_physicalDevice(physicalDevice), m_objectNames(objectNames) {
    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_properties);
    const QueueFamilies families = pickQueueFamilies(m_physicalDevice, surface);
    m_graphicsFamily = families.graphics;
    m_presentFamily = surface != VK_NULL_HANDLE ? families.present : families.graphics;

    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (std::uint32_t family : {m_graphicsFamily, m_presentFamily}) {
        if (!queueInfos.empty() && queueInfos.front().queueFamilyIndex == family) {
            continue;
        }
        VkDeviceQueueCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &priority;
        queueInfos.push_back(info);
    }

    std::vector<const char *> extensions;
    if (surface != VK_NULL_HANDLE) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    // Required by Phox (checked when the device is picked): barriers and submits use synchronization2,
    // and graphics passes use dynamic rendering instead of render pass objects.
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.synchronization2 = VK_TRUE;
    features13.dynamicRendering = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pNext = &features;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create the logical device");
    }
    volkLoadDeviceTable(&m_table, m_device);
    m_table.vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    m_table.vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);

    // VMA is built without static or dynamic function loading, so it gets every entry point from here.
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    functions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    functions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    functions.vkAllocateMemory = m_table.vkAllocateMemory;
    functions.vkFreeMemory = m_table.vkFreeMemory;
    functions.vkMapMemory = m_table.vkMapMemory;
    functions.vkUnmapMemory = m_table.vkUnmapMemory;
    functions.vkFlushMappedMemoryRanges = m_table.vkFlushMappedMemoryRanges;
    functions.vkInvalidateMappedMemoryRanges = m_table.vkInvalidateMappedMemoryRanges;
    functions.vkBindBufferMemory = m_table.vkBindBufferMemory;
    functions.vkBindImageMemory = m_table.vkBindImageMemory;
    functions.vkGetBufferMemoryRequirements = m_table.vkGetBufferMemoryRequirements;
    functions.vkGetImageMemoryRequirements = m_table.vkGetImageMemoryRequirements;
    functions.vkCreateBuffer = m_table.vkCreateBuffer;
    functions.vkDestroyBuffer = m_table.vkDestroyBuffer;
    functions.vkCreateImage = m_table.vkCreateImage;
    functions.vkDestroyImage = m_table.vkDestroyImage;
    functions.vkCmdCopyBuffer = m_table.vkCmdCopyBuffer;
    // Vulkan 1.1 core entry points; VMA reads them through its *2KHR slots.
    functions.vkGetBufferMemoryRequirements2KHR = m_table.vkGetBufferMemoryRequirements2;
    functions.vkGetImageMemoryRequirements2KHR = m_table.vkGetImageMemoryRequirements2;
    functions.vkBindBufferMemory2KHR = m_table.vkBindBufferMemory2;
    functions.vkBindImageMemory2KHR = m_table.vkBindImageMemory2;
    functions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
    // Vulkan 1.3 core.
    functions.vkGetDeviceBufferMemoryRequirements = m_table.vkGetDeviceBufferMemoryRequirements;
    functions.vkGetDeviceImageMemoryRequirements = m_table.vkGetDeviceImageMemoryRequirements;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;
    allocatorInfo.vulkanApiVersion = PHOX_VULKAN_API_VERSION; // must match the instance
    allocatorInfo.pVulkanFunctions = &functions;
    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
        m_table.vkDestroyDevice(m_device, nullptr);
        throw std::runtime_error("failed to create the memory allocator");
    }
}

Device::~Device() {
    m_table.vkDeviceWaitIdle(m_device);
    vmaDestroyAllocator(m_allocator);
    m_table.vkDestroyDevice(m_device, nullptr);
}

void Device::waitIdle() { m_table.vkDeviceWaitIdle(m_device); }

void Device::setObjectName(VkObjectType type, std::uint64_t handle, const std::string &name) const {
    if (!m_objectNames || name.empty() || vkSetDebugUtilsObjectNameEXT == nullptr) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name.c_str();
    vkSetDebugUtilsObjectNameEXT(m_device, &info);
}

std::unique_ptr<phox::Buffer> Device::createBuffer(const BufferCreateInfo &info) { return makeBuffer(info); }

std::unique_ptr<phox::Image> Device::createImage(const ImageCreateInfo &info) { return makeImage(info); }

std::unique_ptr<Buffer> Device::makeBuffer(const BufferCreateInfo &info) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = info.size;
    bufferInfo.usage = toVkBufferUsage(info.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // buffers are only used on the graphics queue
    if (bufferInfo.usage == 0) {
        throw std::logic_error("buffer '" + info.name + "' has no GPU usage");
    }

    const VmaAllocationCreateInfo allocationInfo = allocationInfoFor(info.memory);
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo allocated{};
    if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocationInfo, &buffer, &allocation, &allocated) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer '" + info.name + "'");
    }
    setObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(buffer), info.name);
    return std::make_unique<Buffer>(*this, buffer, allocation, info.size, allocated.pMappedData);
}

std::unique_ptr<Image> Device::makeImage(const ImageCreateInfo &info) {
    const VkFormat format = toVkFormat(info.format);
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {info.extent.width, info.extent.height, 1};
    imageInfo.mipLevels = info.mipLevels;
    imageInfo.arrayLayers = info.arrayLayers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = toVkImageUsage(info.usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (imageInfo.usage == 0) {
        throw std::logic_error("image '" + info.name + "' has no GPU usage");
    }

    const VmaAllocationCreateInfo allocationInfo = allocationInfoFor(MemoryLocation::DeviceLocal);
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    if (vmaCreateImage(m_allocator, &imageInfo, &allocationInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image '" + info.name + "'");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = info.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {aspectMask(info.format), 0, info.mipLevels, 0, info.arrayLayers};
    VkImageView view = VK_NULL_HANDLE;
    if (m_table.vkCreateImageView(m_device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        vmaDestroyImage(m_allocator, image, allocation);
        throw std::runtime_error("failed to create a view of image '" + info.name + "'");
    }
    setObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(image), info.name);
    setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(view), info.name);
    return std::make_unique<Image>(*this, info, image, allocation, view);
}

} // namespace phox::vk
