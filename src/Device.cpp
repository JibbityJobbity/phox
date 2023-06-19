#define VMA_IMPLEMENTATION
#include "Device.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace phox {

VkPhysicalDevice Device::PickPhysicalDevice(VkInstance instance,
                                            DeviceType type) {
    uint32_t nPhysDev;
    vkEnumeratePhysicalDevices(instance, &nPhysDev, nullptr);
    auto physDevs = std::vector<VkPhysicalDevice>(nPhysDev);
    vkEnumeratePhysicalDevices(instance, &nPhysDev, physDevs.data());

    VkPhysicalDeviceType physDevType;
    switch (type) {
    case DeviceType::Discrete:
        physDevType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
        break;
    case DeviceType::Integrated:
        physDevType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        break;
    case DeviceType::Virtual:
        physDevType = VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU;
        break;
    case DeviceType::CPU:
        physDevType = VK_PHYSICAL_DEVICE_TYPE_CPU;
        break;
    default:
        physDevType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
        break;
    };

    VkPhysicalDevice out = VK_NULL_HANDLE;

    for (size_t i = 0; i < nPhysDev; i++) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(physDevs[i], &props);
        if (props.deviceType == physDevType) {
            out = physDevs[i];
            break;
        }
    }

    return out;
}

void Device::initialize(VkPhysicalDevice physicalDevice, VkInstance instance,
                        VkSurfaceKHR surface) {
    VkResult result;
    m_instance = instance;
    m_surface = surface;
    m_physicalDevice = physicalDevice;

    // Pick queue families
    uint32_t nQueueFamilies = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &nQueueFamilies,
                                             nullptr);
    auto *queueFamilies = new VkQueueFamilyProperties[nQueueFamilies];
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &nQueueFamilies,
                                             queueFamilies);
    uint32_t graphicsFamily = -1;
    uint32_t presentFamily = -1;
    VkBool32 presentSupport = VK_FALSE;
    for (uint32_t i = 0; i < nQueueFamilies; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamily = i;
            continue;
        }
        vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface,
                                             &presentSupport);
        if (presentSupport) {
            presentFamily = i;
            continue;
        }
    }

    delete[] queueFamilies;

    // Create logical device
    {
        float queuePriority = 1.0f;
        VkDeviceQueueCreateInfo graphicsQueueCreateInfo = {};
        graphicsQueueCreateInfo.sType =
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        graphicsQueueCreateInfo.queueFamilyIndex = graphicsFamily;
        graphicsQueueCreateInfo.queueCount = 1;
        graphicsQueueCreateInfo.pQueuePriorities = &queuePriority;

        VkDeviceQueueCreateInfo presentQueueCreateInfo = {};
        presentQueueCreateInfo.sType =
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        presentQueueCreateInfo.queueFamilyIndex = presentFamily;
        presentQueueCreateInfo.queueCount = 1;
        presentQueueCreateInfo.pQueuePriorities = &queuePriority;

        VkDeviceQueueCreateInfo qci[] = {graphicsQueueCreateInfo,
                                         presentQueueCreateInfo};

        VkPhysicalDeviceFeatures deviceFeatures = {};
        vkGetPhysicalDeviceFeatures(m_physicalDevice, &deviceFeatures);
        // TODO: Enable required features

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pQueueCreateInfos = qci;
        deviceCreateInfo.queueCreateInfoCount = sizeof(qci) / sizeof(qci[0]);
        deviceCreateInfo.pEnabledFeatures = &deviceFeatures;
        deviceCreateInfo.enabledExtensionCount =
            0; // TODO: Enable required extensions
        deviceCreateInfo.enabledLayerCount = 0; // TODO: Enable requried layers

        result = vkCreateDevice(m_physicalDevice, &deviceCreateInfo, nullptr,
                                &m_device);
        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to create logical device!");
        }
    }

    volkLoadDeviceTable(&m_table, m_device);

    {
        m_table.vkGetDeviceQueue(m_device, graphicsFamily, 0,
                                 &m_queueInfo.graphicsQueue);
        m_table.vkGetDeviceQueue(m_device, presentFamily, 0,
                                 &m_queueInfo.presentQueue);
        m_queueInfo.graphicsQueueIndex = graphicsFamily;
        m_queueInfo.presentQueueIndex = presentFamily;
    }

    // Memory allocator
    {
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        vulkanFunctions.vkGetPhysicalDeviceProperties =
            vkGetPhysicalDeviceProperties;
        vulkanFunctions.vkGetPhysicalDeviceMemoryProperties =
            vkGetPhysicalDeviceMemoryProperties;
        vulkanFunctions.vkAllocateMemory = m_table.vkAllocateMemory;
        vulkanFunctions.vkFreeMemory = m_table.vkFreeMemory;
        vulkanFunctions.vkMapMemory = m_table.vkMapMemory;
        vulkanFunctions.vkUnmapMemory = m_table.vkUnmapMemory;
        vulkanFunctions.vkFlushMappedMemoryRanges =
            m_table.vkFlushMappedMemoryRanges;
        vulkanFunctions.vkInvalidateMappedMemoryRanges =
            m_table.vkInvalidateMappedMemoryRanges;
        vulkanFunctions.vkBindBufferMemory = m_table.vkBindBufferMemory;
        vulkanFunctions.vkBindImageMemory = m_table.vkBindImageMemory;
        vulkanFunctions.vkGetBufferMemoryRequirements =
            m_table.vkGetBufferMemoryRequirements;
        vulkanFunctions.vkGetImageMemoryRequirements =
            m_table.vkGetImageMemoryRequirements;
        vulkanFunctions.vkCreateBuffer = m_table.vkCreateBuffer;
        vulkanFunctions.vkDestroyBuffer = m_table.vkDestroyBuffer;
        vulkanFunctions.vkCreateImage = m_table.vkCreateImage;
        vulkanFunctions.vkDestroyImage = m_table.vkDestroyImage;
        vulkanFunctions.vkCmdCopyBuffer = m_table.vkCmdCopyBuffer;
        vulkanFunctions.vkGetBufferMemoryRequirements2KHR =
            m_table.vkGetBufferMemoryRequirements2KHR;
        vulkanFunctions.vkGetImageMemoryRequirements2KHR =
            m_table.vkGetImageMemoryRequirements2KHR;
        vulkanFunctions.vkBindBufferMemory2KHR = m_table.vkBindBufferMemory2KHR;
        vulkanFunctions.vkBindImageMemory2KHR = m_table.vkBindImageMemory2KHR;
        vulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR =
            vkGetPhysicalDeviceMemoryProperties2KHR;
        vulkanFunctions.vkGetDeviceBufferMemoryRequirements =
            vkGetDeviceBufferMemoryRequirements;
        vulkanFunctions.vkGetDeviceImageMemoryRequirements =
            vkGetDeviceImageMemoryRequirements;

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = m_physicalDevice;
        allocatorInfo.device = m_device;
        allocatorInfo.instance = m_instance;
        allocatorInfo.vulkanApiVersion = VK_MAKE_VERSION(1, 3, 0);
        allocatorInfo.pVulkanFunctions = &vulkanFunctions;
        result = vmaCreateAllocator(&allocatorInfo, &m_allocator);

        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to create memory allocator!");
        }
    }

    m_initialized = true;
}

void Device::cleanup() {
    if (!m_initialized)
        return;

    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }

    if (m_device != VK_NULL_HANDLE) {
        m_table.vkQueueWaitIdle(m_queueInfo.graphicsQueue);
        m_table.vkQueueWaitIdle(m_queueInfo.presentQueue);
        m_table.vkDeviceWaitIdle(m_device);
        m_table.vkDestroyDevice(m_device, nullptr);
        m_queueInfo.graphicsQueue = VK_NULL_HANDLE;
        m_queueInfo.presentQueue = VK_NULL_HANDLE;
        m_device = VK_NULL_HANDLE;
    }
    // Device doesn't own these
    m_physicalDevice = VK_NULL_HANDLE;
    m_instance = VK_NULL_HANDLE;
    m_initialized = false;
}

bool Device::isInitialized() { return m_initialized; }

Device::~Device() { cleanup(); }

Buffer
Device::allocateBuffer(size_t size, VkBufferUsageFlagBits usage,
                       MemoryLocation memoryLocation = MemoryLocation::Auto) {
    const VmaMemoryUsage memLocationToUsage[] = {
        VMA_MEMORY_USAGE_AUTO,
        VMA_MEMORY_USAGE_GPU_ONLY,
        VMA_MEMORY_USAGE_CPU_ONLY,
        VMA_MEMORY_USAGE_CPU_COPY,
    };

    VkBuffer buf;
    VmaAllocation allocation;

    uint32_t queueFamilyIndices[] = {m_queueInfo.presentQueueIndex,
                                     m_queueInfo.graphicsQueueIndex};

    VkBufferCreateInfo bufferCreateInfo;
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.pNext = nullptr;
    bufferCreateInfo.flags = 0;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage = usage;
    bufferCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
    bufferCreateInfo.queueFamilyIndexCount =
        sizeof(queueFamilyIndices) / sizeof(queueFamilyIndices[0]);
    bufferCreateInfo.pQueueFamilyIndices = queueFamilyIndices;

    VmaAllocationCreateInfo allocationCreateInfo;
    allocationCreateInfo.usage = memLocationToUsage[(size_t)memoryLocation];

    vmaCreateBuffer(m_allocator, &bufferCreateInfo, &allocationCreateInfo, &buf,
                    &allocation, nullptr);

    return {size, buf, m_allocator, allocation};
}

void Device::freeBuffer(Buffer &buffer) { buffer.free(); }

Image Device::allocateImage(const size_t width, const size_t height) {
    VkResult res;
    VmaAllocation allocation;
    VkImage image = VK_NULL_HANDLE;
    const VkImageCreateInfo createInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R8G8B8A8_SRGB,
        .extent =
            {
                .width = static_cast<uint32_t>(width),
                .height = static_cast<uint32_t>(height),
                .depth = 1,
            },
        .mipLevels = 1,
        .arrayLayers = 1,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .sharingMode = VK_SHARING_MODE_CONCURRENT,
    };

    VmaAllocationCreateInfo allocationCreateInfo;
    allocationCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    VkBindImageMemoryInfo bindInfo;
    bindInfo.sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;

    res = vmaCreateImage(m_allocator, &createInfo, &allocationCreateInfo,
                         &image, &allocation, nullptr);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image!");
    }

    return {image, m_device, m_allocator, allocation};
}

} // namespace phox
