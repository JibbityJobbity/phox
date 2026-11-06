#define VMA_IMPLEMENTATION

#include "phox/vulkan/Device.hpp"

#include "phox/Log.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Conversions.hpp"
#include "phox/vulkan/Image.hpp"

#include <algorithm>
#include <cstring>
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

// A family that computes but cannot draw: its queues run alongside the graphics queue on hardware with
// separate compute engines. UINT32_MAX if there is none.
std::uint32_t pickAsyncComputeFamily(VkPhysicalDevice physicalDevice) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, families.data());
    for (std::uint32_t i = 0; i < count; ++i) {
        const VkQueueFlags flags = families[i].queueFlags;
        if ((flags & VK_QUEUE_COMPUTE_BIT) != 0 && (flags & VK_QUEUE_GRAPHICS_BIT) == 0 && families[i].queueCount > 0) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool hasDeviceExtension(VkPhysicalDevice physicalDevice, const char *name) {
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(),
                       [&](const VkExtensionProperties &e) { return std::strcmp(e.extensionName, name) == 0; });
}

} // namespace

bool hasBindlessTextures(const VkPhysicalDeviceVulkan12Features &features) {
    return features.descriptorIndexing && features.runtimeDescriptorArray && features.descriptorBindingPartiallyBound &&
           features.descriptorBindingSampledImageUpdateAfterBind && features.descriptorBindingUpdateUnusedWhilePending &&
           features.shaderSampledImageArrayNonUniformIndexing;
}

namespace {

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

Device::Device(VkInstance instance, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, bool objectNames,
               bool meshShaders, bool asyncCompute)
    : m_instance(instance), m_physicalDevice(physicalDevice), m_objectNames(objectNames) {
    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_properties);
    const QueueFamilies families = pickQueueFamilies(m_physicalDevice, surface);
    m_graphicsFamily = families.graphics;
    m_presentFamily = surface != VK_NULL_HANDLE ? families.present : families.graphics;
    if (asyncCompute) {
        m_computeFamily = pickAsyncComputeFamily(m_physicalDevice);
    }

    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (std::uint32_t family : {m_graphicsFamily, m_presentFamily, m_computeFamily}) {
        const bool known = std::any_of(queueInfos.begin(), queueInfos.end(),
                                       [&](const VkDeviceQueueCreateInfo &info) { return info.queueFamilyIndex == family; });
        if (family == UINT32_MAX || known) {
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
    // A portability implementation (MoltenVK) must have its subset enabled whenever it exposes it.
    if (hasDeviceExtension(m_physicalDevice, "VK_KHR_portability_subset")) {
        extensions.push_back("VK_KHR_portability_subset");
    }

    // Required by Phox (checked when the device is picked): barriers and submits use synchronization2,
    // and graphics passes use dynamic rendering instead of render pass objects. Buffer device addresses
    // (which Vulkan 1.3 guarantees) let mesh shaders read mesh buffers through push constants.
    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    // The texture table (hasBindlessTextures): textures are added while earlier frames are still in flight.
    features12.descriptorIndexing = VK_TRUE;
    features12.runtimeDescriptorArray = VK_TRUE;
    features12.descriptorBindingPartiallyBound = VK_TRUE;
    features12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    features12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    features12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    // Submissions to the two queues wait for each other through timeline semaphores (required by 1.2).
    features12.timelineSemaphore = VK_TRUE;
    {
        // Optional: resetting timestamp queries from the CPU, so passes on either queue can be timed.
        VkPhysicalDeviceVulkan12Features supported12{};
        supported12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        VkPhysicalDeviceFeatures2 supported{};
        supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supported.pNext = &supported12;
        vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supported);
        m_hostQueryReset = supported12.hostQueryReset == VK_TRUE;
        features12.hostQueryReset = supported12.hostQueryReset;
    }
    {
        VkPhysicalDeviceVulkan12Properties properties12{};
        properties12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES;
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &properties12;
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties);
        m_bindlessTextureCount = std::min({kMaxBindlessTextures, properties12.maxDescriptorSetUpdateAfterBindSampledImages,
                                           properties12.maxPerStageDescriptorUpdateAfterBindSampledImages,
                                           properties12.maxDescriptorSetUpdateAfterBindSamplers,
                                           properties12.maxPerStageDescriptorUpdateAfterBindSamplers});
    }
    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.pNext = &features12;
    features13.synchronization2 = VK_TRUE;
    features13.dynamicRendering = VK_TRUE;
    // Also guaranteed by 1.3; SPIR-V built for Vulkan 1.3 (the task and mesh shaders) declares workgroup
    // sizes with LocalSizeId, which needs it.
    features13.maintenance4 = VK_TRUE;
    VkPhysicalDeviceFeatures2 features{};
    features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    features.pNext = &features13;

    // Optional: task and mesh shaders. Without them the forward pass draws with vertex shaders.
    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{};
    meshFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
    if (meshShaders && hasDeviceExtension(m_physicalDevice, VK_EXT_MESH_SHADER_EXTENSION_NAME)) {
        VkPhysicalDeviceFeatures2 supported{};
        supported.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        supported.pNext = &meshFeatures;
        vkGetPhysicalDeviceFeatures2(m_physicalDevice, &supported);
        m_meshShaders = meshFeatures.taskShader == VK_TRUE && meshFeatures.meshShader == VK_TRUE;
    }
    if (m_meshShaders) {
        // Only what Phox uses: enabling e.g. multiviewMeshShader would require multiview too.
        meshFeatures = {};
        meshFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
        meshFeatures.taskShader = VK_TRUE;
        meshFeatures.meshShader = VK_TRUE;
        features12.pNext = &meshFeatures;
        extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);

        m_meshShaderProperties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT;
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &m_meshShaderProperties;
        vkGetPhysicalDeviceProperties2(m_physicalDevice, &properties);
        m_meshShaderProperties.pNext = nullptr;
    }

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
    if (m_computeFamily != UINT32_MAX) {
        m_table.vkGetDeviceQueue(m_device, m_computeFamily, 0, &m_computeQueue);
    }

    VkSemaphoreTypeCreateInfo timelineType{};
    timelineType.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &timelineType;
    for (QueueTimeline &timeline : m_timelines) {
        if (m_table.vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &timeline.semaphore) != VK_SUCCESS) {
            for (QueueTimeline &created : m_timelines) m_table.vkDestroySemaphore(m_device, created.semaphore, nullptr);
            m_table.vkDestroyDevice(m_device, nullptr);
            throw std::runtime_error("failed to create a timeline semaphore");
        }
    }

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
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.pVulkanFunctions = &functions;
    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
        for (QueueTimeline &timeline : m_timelines) m_table.vkDestroySemaphore(m_device, timeline.semaphore, nullptr);
        m_table.vkDestroyDevice(m_device, nullptr);
        throw std::runtime_error("failed to create the memory allocator");
    }
    setObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<std::uint64_t>(m_timelines[0].semaphore), "graphics timeline");
    setObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<std::uint64_t>(m_timelines[1].semaphore),
                  "async compute timeline");
    if (m_computeQueue != VK_NULL_HANDLE) {
        setObjectName(VK_OBJECT_TYPE_QUEUE, reinterpret_cast<std::uint64_t>(m_computeQueue), "async compute");
    }
}

Device::~Device() {
    m_table.vkDeviceWaitIdle(m_device);
    for (QueueTimeline &timeline : m_timelines) m_table.vkDestroySemaphore(m_device, timeline.semaphore, nullptr);
    vmaDestroyAllocator(m_allocator);
    m_table.vkDestroyDevice(m_device, nullptr);
}

void Device::waitIdle() { m_table.vkDeviceWaitIdle(m_device); }

VkQueue Device::queue(Queue queue) const {
    if (queue == Queue::AsyncCompute) {
        if (m_computeQueue == VK_NULL_HANDLE) throw std::logic_error("the device has no async compute queue");
        return m_computeQueue;
    }
    return m_graphicsQueue;
}

std::uint32_t Device::queueFamilyIndex(Queue queue) const {
    return queue == Queue::AsyncCompute ? m_computeFamily : m_graphicsFamily;
}

std::uint64_t Device::signalValue(Queue queue) { return ++m_timelines[static_cast<std::size_t>(queue)].value; }

void Device::waitTimeline(Queue queue, std::uint64_t value) const {
    if (value == 0) {
        return;
    }
    const VkSemaphore semaphore = timeline(queue);
    VkSemaphoreWaitInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    info.semaphoreCount = 1;
    info.pSemaphores = &semaphore;
    info.pValues = &value;
    if (m_table.vkWaitSemaphores(m_device, &info, UINT64_MAX) != VK_SUCCESS) {
        throw std::runtime_error("waiting for a timeline semaphore failed");
    }
}

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
    if (info.deviceAddress) {
        bufferInfo.usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    }
    // With async compute, every buffer can be used on both queues: no queue family ownership transfers,
    // which buffers lose nothing by.
    const std::uint32_t sharedFamilies[2] = {m_graphicsFamily, m_computeFamily};
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (asyncCompute()) {
        bufferInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        bufferInfo.queueFamilyIndexCount = 2;
        bufferInfo.pQueueFamilyIndices = sharedFamilies;
    }
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
    VkDeviceAddress address = 0;
    if (info.deviceAddress) {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer;
        address = m_table.vkGetBufferDeviceAddress(m_device, &addressInfo);
    }
    return std::make_unique<Buffer>(*this, buffer, allocation, info.size, allocated.pMappedData, address);
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
    // Images stay exclusive unless they are used on both queues: concurrent sharing can cost compression.
    const std::uint32_t sharedFamilies[2] = {m_graphicsFamily, m_computeFamily};
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (info.sharedQueues && asyncCompute()) {
        imageInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        imageInfo.queueFamilyIndexCount = 2;
        imageInfo.pQueueFamilyIndices = sharedFamilies;
    }
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
