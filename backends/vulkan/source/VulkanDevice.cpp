#include "VulkanDevice.hpp"

#include "VulkanCommandList.hpp"

#include "phox/Log.hpp"

#include <algorithm>
#include <cstring>
#include <set>
#include <string>

namespace phox::vulkan {

namespace {

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                             VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT *data, void *) {
    LogLevel level = LogLevel::Debug;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        level = LogLevel::Error;
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        level = LogLevel::Warning;
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        level = LogLevel::Info;
    }
    std::string message;
    if (data->pMessageIdName != nullptr) {
        message += std::string("[") + data->pMessageIdName + "] ";
    }
    message += data->pMessage != nullptr ? data->pMessage : "";
    log(level, message);
    return VK_FALSE;
}

VkBufferUsageFlags toVk(rhi::BufferUsage usage) {
    VkBufferUsageFlags flags = 0;
    const auto has = [&](rhi::BufferUsage bit) { return (usage & bit) != rhi::BufferUsage{}; };
    if (has(rhi::BufferUsage::Vertex)) flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (has(rhi::BufferUsage::Index)) flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (has(rhi::BufferUsage::Uniform)) flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (has(rhi::BufferUsage::Storage)) flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
    if (has(rhi::BufferUsage::Indirect)) flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (has(rhi::BufferUsage::TransferSrc)) flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (has(rhi::BufferUsage::TransferDst)) flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    return flags;
}

VkImageUsageFlags toVk(rhi::TextureUsage usage) {
    VkImageUsageFlags flags = 0;
    const auto has = [&](rhi::TextureUsage bit) { return (usage & bit) != rhi::TextureUsage{}; };
    if (has(rhi::TextureUsage::Sampled)) flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (has(rhi::TextureUsage::Storage)) flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (has(rhi::TextureUsage::ColorAttachment)) flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (has(rhi::TextureUsage::DepthAttachment)) flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (has(rhi::TextureUsage::TransferSrc)) flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (has(rhi::TextureUsage::TransferDst)) flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    return flags;
}

std::string versionString(uint32_t version) {
    return std::to_string(VK_VERSION_MAJOR(version)) + "." + std::to_string(VK_VERSION_MINOR(version));
}

} // namespace

VulkanDevice::VulkanDevice(const rhi::DeviceOptions &options) {
    try {
        init(options);
    } catch (...) {
        shutdown();
        throw;
    }
}

VulkanDevice::~VulkanDevice() { shutdown(); }

// ---------------------------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------------------------

void VulkanDevice::init(const rhi::DeviceOptions &options) {
    createInstance(options);
    m_physicalDevice = pickPhysicalDevice(options);
    createLogicalDevice();
    createAllocator();

    VkSemaphoreTypeCreateInfo timelineInfo{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = 0;
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    semaphoreInfo.pNext = &timelineInfo;
    PHOX_VK_CHECK(m_table.vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &m_timeline));

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = m_queueFamily;
    PHOX_VK_CHECK(m_table.vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool));

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    PHOX_VK_CHECK(m_table.vkCreateSampler(m_device, &samplerInfo, nullptr, &m_defaultSampler));
}

void VulkanDevice::createInstance(const rhi::DeviceOptions &options) {
    if (volkInitialize() != VK_SUCCESS) {
        throw std::runtime_error("Vulkan backend is not available: the Vulkan loader (libvulkan) could not be loaded");
    }
    const uint32_t loaderVersion = volkGetInstanceVersion();
    if (loaderVersion < VK_API_VERSION_1_3) {
        throw std::runtime_error("Vulkan backend needs Vulkan 1.3 but the loader reports " + versionString(loaderVersion));
    }

    std::vector<const char *> layers;
    std::vector<const char *> extensions;
    if (options.validation) {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
        std::vector<VkLayerProperties> available(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, available.data());
        const bool found = std::any_of(available.begin(), available.end(), [](const VkLayerProperties &p) {
            return std::strcmp(p.layerName, "VK_LAYER_KHRONOS_validation") == 0;
        });
        if (!found) {
            throw std::runtime_error("Vulkan validation was requested but VK_LAYER_KHRONOS_validation is not installed");
        }
        layers.push_back("VK_LAYER_KHRONOS_validation");
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        m_debugUtils = true;
    }

    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "Phox";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Phox";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result == VK_ERROR_INCOMPATIBLE_DRIVER) {
        throw std::runtime_error("Vulkan backend is not available: no Vulkan 1.3 driver found");
    }
    if (result != VK_SUCCESS) {
        throwVk("vkCreateInstance", result);
    }
    volkLoadInstance(m_instance);

    if (m_debugUtils) {
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = debugCallback;
        PHOX_VK_CHECK(vkCreateDebugUtilsMessengerEXT(m_instance, &messengerInfo, nullptr, &m_messenger));
    }
}

VkPhysicalDevice VulkanDevice::pickPhysicalDevice(const rhi::DeviceOptions &options) {
    uint32_t count = 0;
    PHOX_VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, nullptr));
    if (count == 0) {
        throw std::runtime_error("Vulkan backend is not available: no physical devices found");
    }
    std::vector<VkPhysicalDevice> devices(count);
    PHOX_VK_CHECK(vkEnumeratePhysicalDevices(m_instance, &count, devices.data()));

    if (options.adapterIndex) {
        if (*options.adapterIndex >= count) {
            throw std::runtime_error("Vulkan adapterIndex " + std::to_string(*options.adapterIndex) +
                                     " is out of range (" + std::to_string(count) + " adapters)");
        }
        return devices[*options.adapterIndex];
    }
    for (const VkPhysicalDeviceType wanted : {VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU, VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU}) {
        for (const VkPhysicalDevice device : devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            if (props.deviceType == wanted) {
                return device;
            }
        }
    }
    return devices.front();
}

void VulkanDevice::createLogicalDevice() {
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(m_physicalDevice, &props);
    if (props.apiVersion < VK_API_VERSION_1_3) {
        throw std::runtime_error(std::string("Vulkan device '") + props.deviceName + "' reports Vulkan " +
                                 versionString(props.apiVersion) + "; Phox needs 1.3");
    }

    uint32_t extCount = 0;
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, nullptr);
    std::vector<VkExtensionProperties> extProps(extCount);
    vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &extCount, extProps.data());
    std::set<std::string> available;
    for (const auto &e : extProps) {
        available.insert(e.extensionName);
    }
    const auto has = [&](const char *name) { return available.count(name) != 0; };

    // ---- query ----
    VkPhysicalDeviceVulkan12Features q12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    VkPhysicalDeviceVulkan13Features q13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    VkPhysicalDeviceMeshShaderFeaturesEXT qMesh{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR qRt{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR qAs{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR qFsr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    VkPhysicalDeviceFeatures2 q2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

    const bool haveMeshExt = has(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    const bool haveRtExts = has(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
                            has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
                            has(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    const bool haveFsrExt = has(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);

    void **tail = &q2.pNext;
    const auto link = [&](auto &structure) {
        *tail = &structure;
        tail = &structure.pNext;
    };
    link(q12);
    link(q13);
    if (haveMeshExt) link(qMesh);
    if (haveRtExts) {
        link(qRt);
        link(qAs);
    }
    if (haveFsrExt) link(qFsr);
    vkGetPhysicalDeviceFeatures2(m_physicalDevice, &q2);

    std::vector<std::string> missing;
    if (!q13.dynamicRendering) missing.push_back("dynamicRendering");
    if (!q13.synchronization2) missing.push_back("synchronization2");
    if (!q12.timelineSemaphore) missing.push_back("timelineSemaphore");
    if (!q12.bufferDeviceAddress) missing.push_back("bufferDeviceAddress");
    if (!missing.empty()) {
        std::string list;
        for (const auto &m : missing) {
            list += (list.empty() ? "" : ", ") + m;
        }
        throw std::runtime_error(std::string("Vulkan device '") + props.deviceName + "' lacks required features: " + list);
    }

    const bool meshOk = haveMeshExt && qMesh.meshShader && qMesh.taskShader;
    const bool rtOk = haveRtExts && qRt.rayTracingPipeline && qAs.accelerationStructure;
    const bool bindlessOk = q12.runtimeDescriptorArray && q12.descriptorBindingPartiallyBound &&
                            q12.descriptorBindingVariableDescriptorCount && q12.shaderSampledImageArrayNonUniformIndexing;
    const bool drawIndirectCountOk = q12.drawIndirectCount;
    const bool float16Ok = q12.shaderFloat16;
    const bool atomic64Ok = q12.shaderBufferInt64Atomics && q2.features.shaderInt64;
    const bool vrsOk = haveFsrExt && qFsr.pipelineFragmentShadingRate;

    // ---- enable ----
    VkPhysicalDeviceVulkan12Features e12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    e12.timelineSemaphore = VK_TRUE;
    e12.bufferDeviceAddress = VK_TRUE;
    e12.runtimeDescriptorArray = bindlessOk;
    e12.descriptorBindingPartiallyBound = bindlessOk;
    e12.descriptorBindingVariableDescriptorCount = bindlessOk;
    e12.shaderSampledImageArrayNonUniformIndexing = bindlessOk;
    e12.drawIndirectCount = drawIndirectCountOk;
    e12.shaderFloat16 = float16Ok;
    e12.shaderBufferInt64Atomics = atomic64Ok;
    VkPhysicalDeviceVulkan13Features e13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
    e13.dynamicRendering = VK_TRUE;
    e13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceMeshShaderFeaturesEXT eMesh{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    eMesh.meshShader = VK_TRUE;
    eMesh.taskShader = VK_TRUE;
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR eRt{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    eRt.rayTracingPipeline = VK_TRUE;
    VkPhysicalDeviceAccelerationStructureFeaturesKHR eAs{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    eAs.accelerationStructure = VK_TRUE;
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR eFsr{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    eFsr.pipelineFragmentShadingRate = VK_TRUE;
    VkPhysicalDeviceFeatures2 e2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    e2.features.shaderInt64 = atomic64Ok ? VK_TRUE : VK_FALSE;

    std::vector<const char *> extensions;
    tail = &e2.pNext;
    link(e12);
    link(e13);
    if (meshOk) {
        link(eMesh);
        extensions.push_back(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    }
    if (rtOk) {
        link(eRt);
        link(eAs);
        extensions.push_back(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
        extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    }
    if (vrsOk) {
        link(eFsr);
        extensions.push_back(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
    }
    m_pushDescriptors = has(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    if (m_pushDescriptors) {
        extensions.push_back(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    }
    m_meshShaders = meshOk;

    // ---- queue ----
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, families.data());
    uint32_t family = UINT32_MAX;
    for (uint32_t i = 0; i < familyCount; ++i) {
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            family = i;
            break;
        }
    }
    if (family == UINT32_MAX) {
        throw std::runtime_error(std::string("Vulkan device '") + props.deviceName + "' has no graphics queue family");
    }
    m_queueFamily = family;

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    deviceInfo.pNext = &e2;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    deviceInfo.ppEnabledExtensionNames = extensions.data();
    PHOX_VK_CHECK(vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device));
    volkLoadDeviceTable(&m_table, m_device);
    m_table.vkGetDeviceQueue(m_device, family, 0, &m_queue);

    // ---- capabilities ----
    m_capabilities.deviceName = props.deviceName;
    m_capabilities.apiMajor = VK_VERSION_MAJOR(props.apiVersion);
    m_capabilities.apiMinor = VK_VERSION_MINOR(props.apiVersion);
    m_capabilities.maxTextureSize2D = props.limits.maxImageDimension2D;
    m_capabilities.maxPushConstantBytes = props.limits.maxPushConstantsSize;
    const auto set = [&](rhi::Feature f, bool on) { m_capabilities.features.set(static_cast<size_t>(f), on); };
    set(rhi::Feature::MeshShader, meshOk);
    set(rhi::Feature::RayTracing, rtOk);
    set(rhi::Feature::BindlessDescriptors, bindlessOk);
    set(rhi::Feature::DrawIndirectCount, drawIndirectCountOk);
    set(rhi::Feature::ShaderFloat16, float16Ok);
    set(rhi::Feature::Atomic64, atomic64Ok);
    set(rhi::Feature::VariableRateShading, vrsOk);
}

void VulkanDevice::createAllocator() {
    VmaVulkanFunctions fn{};
    fn.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    fn.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    fn.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    fn.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    fn.vkAllocateMemory = m_table.vkAllocateMemory;
    fn.vkFreeMemory = m_table.vkFreeMemory;
    fn.vkMapMemory = m_table.vkMapMemory;
    fn.vkUnmapMemory = m_table.vkUnmapMemory;
    fn.vkFlushMappedMemoryRanges = m_table.vkFlushMappedMemoryRanges;
    fn.vkInvalidateMappedMemoryRanges = m_table.vkInvalidateMappedMemoryRanges;
    fn.vkBindBufferMemory = m_table.vkBindBufferMemory;
    fn.vkBindImageMemory = m_table.vkBindImageMemory;
    fn.vkGetBufferMemoryRequirements = m_table.vkGetBufferMemoryRequirements;
    fn.vkGetImageMemoryRequirements = m_table.vkGetImageMemoryRequirements;
    fn.vkCreateBuffer = m_table.vkCreateBuffer;
    fn.vkDestroyBuffer = m_table.vkDestroyBuffer;
    fn.vkCreateImage = m_table.vkCreateImage;
    fn.vkDestroyImage = m_table.vkDestroyImage;
    fn.vkCmdCopyBuffer = m_table.vkCmdCopyBuffer;
    fn.vkGetBufferMemoryRequirements2KHR = m_table.vkGetBufferMemoryRequirements2;
    fn.vkGetImageMemoryRequirements2KHR = m_table.vkGetImageMemoryRequirements2;
    fn.vkBindBufferMemory2KHR = m_table.vkBindBufferMemory2;
    fn.vkBindImageMemory2KHR = m_table.vkBindImageMemory2;
    fn.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;
    fn.vkGetDeviceBufferMemoryRequirements = m_table.vkGetDeviceBufferMemoryRequirements;
    fn.vkGetDeviceImageMemoryRequirements = m_table.vkGetDeviceImageMemoryRequirements;

    VmaAllocatorCreateInfo info{};
    info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    info.physicalDevice = m_physicalDevice;
    info.device = m_device;
    info.instance = m_instance;
    info.vulkanApiVersion = VK_API_VERSION_1_3;
    info.pVulkanFunctions = &fn;
    PHOX_VK_CHECK(vmaCreateAllocator(&info, &m_allocator));
}

// ---------------------------------------------------------------------------------------------
// Teardown. Safe on a partially constructed device.
// ---------------------------------------------------------------------------------------------

void VulkanDevice::shutdown() noexcept {
    try {
        if (m_device != VK_NULL_HANDLE) {
            m_table.vkDeviceWaitIdle(m_device);

            if (m_active) {
                const VkCommandBuffer active = m_active->handle();
                m_table.vkFreeCommandBuffers(m_device, m_commandPool, 1, &active);
                m_active.reset();
            }
            for (InFlight &entry : m_inFlight) {
                retire(entry);
            }
            m_inFlight.clear();
            for (Deferred &d : m_deferred) {
                d.destroy();
            }
            m_deferred.clear();

            m_buffers.forEachAlive([&](BufferData &d) { destroyBufferData(d); });
            m_textures.forEachAlive([&](TextureData &d) { destroyTextureData(d); });
            m_shaders.forEachAlive([&](ShaderData &d) {
                m_table.vkDestroyShaderModule(m_device, d.module, nullptr);
                d.module = VK_NULL_HANDLE;
            });
            m_pipelines.forEachAlive([&](PipelineData &d) { destroyPipelineData(d); });

            for (const VkDescriptorPool pool : m_allDescriptorPools) {
                m_table.vkDestroyDescriptorPool(m_device, pool, nullptr);
            }
            m_allDescriptorPools.clear();
            m_freeDescriptorPools.clear();
            m_table.vkDestroySampler(m_device, m_defaultSampler, nullptr);
            m_table.vkDestroySemaphore(m_device, m_timeline, nullptr);
            m_table.vkDestroyCommandPool(m_device, m_commandPool, nullptr);
            m_defaultSampler = VK_NULL_HANDLE;
            m_timeline = VK_NULL_HANDLE;
            m_commandPool = VK_NULL_HANDLE;
        }
        if (m_allocator != nullptr) {
            vmaDestroyAllocator(m_allocator);
            m_allocator = nullptr;
        }
        if (m_device != VK_NULL_HANDLE) {
            m_table.vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
        if (m_messenger != VK_NULL_HANDLE) {
            vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
            m_messenger = VK_NULL_HANDLE;
        }
        if (m_instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
    } catch (...) {
        // Nothing sensible left to do while tearing down.
    }
}

void VulkanDevice::destroyBufferData(BufferData &data) {
    if (data.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(m_allocator, data.buffer, data.allocation);
    }
    data = {};
}

void VulkanDevice::destroyTextureData(TextureData &data) {
    if (data.view != VK_NULL_HANDLE) {
        m_table.vkDestroyImageView(m_device, data.view, nullptr);
    }
    if (data.image != VK_NULL_HANDLE) {
        vmaDestroyImage(m_allocator, data.image, data.allocation);
    }
    data = {};
}

void VulkanDevice::destroyPipelineData(PipelineData &data) {
    if (data.pipeline != VK_NULL_HANDLE) {
        m_table.vkDestroyPipeline(m_device, data.pipeline, nullptr);
    }
    if (data.layout != VK_NULL_HANDLE) {
        m_table.vkDestroyPipelineLayout(m_device, data.layout, nullptr);
    }
    for (const VkDescriptorSetLayout layout : data.setLayouts) {
        m_table.vkDestroyDescriptorSetLayout(m_device, layout, nullptr);
    }
    data = {};
}

// ---------------------------------------------------------------------------------------------
// Resources
// ---------------------------------------------------------------------------------------------

const char *VulkanDevice::keepName(const char *name) {
    if (name == nullptr) {
        return nullptr;
    }
    m_names.emplace_back(name);
    return m_names.back().c_str();
}

void VulkanDevice::setObjectName(VkObjectType type, uint64_t handle, const char *name) {
    if (!m_debugUtils || name == nullptr || vkSetDebugUtilsObjectNameEXT == nullptr) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(m_device, &info);
}

rhi::BufferHandle VulkanDevice::createBuffer(const rhi::BufferDesc &desc) {
    if (desc.size == 0) {
        throw std::invalid_argument("createBuffer: size must be greater than zero");
    }
    const VkBufferUsageFlags usage = toVk(desc.usage);
    if (usage == 0) {
        throw std::invalid_argument("createBuffer: usage must not be empty");
    }

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = desc.size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    switch (desc.memory) {
    case rhi::MemoryKind::GpuOnly:
        allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case rhi::MemoryKind::Upload:
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case rhi::MemoryKind::Readback:
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }

    BufferData data;
    VmaAllocationInfo info{};
    PHOX_VK_CHECK(vmaCreateBuffer(m_allocator, &bufferInfo, &allocInfo, &data.buffer, &data.allocation, &info));
    data.mapped = info.pMappedData;
    data.desc = desc;
    data.desc.debugName = keepName(desc.debugName);
    setObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(data.buffer), data.desc.debugName);
    return m_buffers.create(std::move(data));
}

rhi::TextureHandle VulkanDevice::createTexture(const rhi::TextureDesc &desc) {
    if (desc.width == 0 || desc.height == 0 || desc.mips == 0) {
        throw std::invalid_argument("createTexture: width, height and mips must be greater than zero");
    }
    if (desc.format == rhi::Format::Undefined) {
        throw std::invalid_argument("createTexture: format must not be Undefined");
    }
    const VkImageUsageFlags usage = toVk(desc.usage);
    if (usage == 0) {
        throw std::invalid_argument("createTexture: usage must not be empty");
    }

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = toVk(desc.format);
    imageInfo.extent = {desc.width, desc.height, 1};
    imageInfo.mipLevels = desc.mips;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

    TextureData data;
    PHOX_VK_CHECK(vmaCreateImage(m_allocator, &imageInfo, &allocInfo, &data.image, &data.allocation, nullptr));

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = data.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange = {aspectOf(desc.format), 0, desc.mips, 0, 1};
    const VkResult viewResult = m_table.vkCreateImageView(m_device, &viewInfo, nullptr, &data.view);
    if (viewResult != VK_SUCCESS) {
        vmaDestroyImage(m_allocator, data.image, data.allocation);
        throwVk("vkCreateImageView", viewResult);
    }

    data.desc = desc;
    data.desc.debugName = keepName(desc.debugName);
    setObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(data.image), data.desc.debugName);
    return m_textures.create(std::move(data));
}

rhi::ShaderHandle VulkanDevice::createShader(const rhi::ShaderDesc &desc) {
    if (desc.spirv.empty()) {
        throw std::invalid_argument("createShader: spirv must not be empty");
    }
    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = desc.spirv.size() * sizeof(uint32_t);
    info.pCode = desc.spirv.data();

    ShaderData data;
    PHOX_VK_CHECK(m_table.vkCreateShaderModule(m_device, &info, nullptr, &data.module));
    data.stage = desc.stage;
    data.entry = desc.entry != nullptr ? desc.entry : "main";
    return m_shaders.create(std::move(data));
}

void VulkanDevice::destroy(rhi::BufferHandle handle) {
    BufferData data;
    if (m_buffers.take(handle, data)) {
        defer([this, data]() mutable { destroyBufferData(data); });
    }
}

void VulkanDevice::destroy(rhi::TextureHandle handle) {
    TextureData data;
    if (m_textures.take(handle, data)) {
        defer([this, data]() mutable { destroyTextureData(data); });
    }
}

void VulkanDevice::destroy(rhi::ShaderHandle handle) {
    ShaderData data;
    if (m_shaders.take(handle, data)) {
        // Command lists never reference shader modules, so no deferral is needed.
        m_table.vkDestroyShaderModule(m_device, data.module, nullptr);
    }
}

void VulkanDevice::destroy(rhi::PipelineHandle handle) {
    PipelineData data;
    if (m_pipelines.take(handle, data)) {
        defer([this, data]() mutable { destroyPipelineData(data); });
    }
}

std::span<std::byte> VulkanDevice::map(rhi::BufferHandle handle) {
    const BufferData &data = m_buffers.get(handle);
    if (data.desc.memory == rhi::MemoryKind::GpuOnly || data.mapped == nullptr) {
        assert(false && "map() requires Upload or Readback memory");
        throw std::logic_error("map: buffer memory is GpuOnly");
    }
    return {static_cast<std::byte *>(data.mapped), static_cast<size_t>(data.desc.size)};
}

const rhi::TextureDesc &VulkanDevice::describe(rhi::TextureHandle handle) const { return m_textures.get(handle).desc; }
const rhi::BufferDesc &VulkanDevice::describe(rhi::BufferHandle handle) const { return m_buffers.get(handle).desc; }

// ---------------------------------------------------------------------------------------------
// Submission and deferred destruction
// ---------------------------------------------------------------------------------------------

uint64_t VulkanDevice::completedTicket() const {
    uint64_t value = 0;
    if (m_timeline != VK_NULL_HANDLE) {
        m_table.vkGetSemaphoreCounterValue(m_device, m_timeline, &value);
    }
    return value;
}

void VulkanDevice::defer(std::function<void()> destroy) {
    if (m_lastTicket <= completedTicket()) {
        destroy();
    } else {
        m_deferred.push_back({m_lastTicket, std::move(destroy)});
    }
}

void VulkanDevice::retire(InFlight &entry) {
    const VkCommandBuffer cb = entry.list->handle();
    m_table.vkFreeCommandBuffers(m_device, m_commandPool, 1, &cb);
    for (const VkDescriptorPool pool : entry.list->takePools()) {
        m_table.vkResetDescriptorPool(m_device, pool, 0);
        m_freeDescriptorPools.push_back(pool);
    }
    entry.list.reset();
}

void VulkanDevice::collect() {
    const uint64_t done = completedTicket();

    std::vector<Deferred> keep;
    for (Deferred &d : m_deferred) {
        if (d.ticket <= done) {
            d.destroy();
        } else {
            keep.push_back(std::move(d));
        }
    }
    m_deferred.swap(keep);

    std::vector<InFlight> stillFlying;
    for (InFlight &entry : m_inFlight) {
        if (entry.ticket <= done) {
            retire(entry);
        } else {
            stillFlying.push_back(std::move(entry));
        }
    }
    m_inFlight.swap(stillFlying);
}

VkDescriptorPool VulkanDevice::acquireDescriptorPool() {
    if (!m_freeDescriptorPools.empty()) {
        const VkDescriptorPool pool = m_freeDescriptorPools.back();
        m_freeDescriptorPools.pop_back();
        return pool;
    }
    const VkDescriptorPoolSize sizes[] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 256},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 256},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 256},
    };
    VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    info.maxSets = 128;
    info.poolSizeCount = 4;
    info.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    PHOX_VK_CHECK(m_table.vkCreateDescriptorPool(m_device, &info, nullptr, &pool));
    m_allDescriptorPools.push_back(pool);
    return pool;
}

rhi::CommandList &VulkanDevice::beginCommands() {
    collect();
    if (m_active) {
        // The previous list was never submitted; drop it.
        const VkCommandBuffer stale = m_active->handle();
        m_table.vkFreeCommandBuffers(m_device, m_commandPool, 1, &stale);
        for (const VkDescriptorPool pool : m_active->takePools()) {
            m_table.vkResetDescriptorPool(m_device, pool, 0);
            m_freeDescriptorPools.push_back(pool);
        }
        m_active.reset();
    }

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = m_commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cb = VK_NULL_HANDLE;
    PHOX_VK_CHECK(m_table.vkAllocateCommandBuffers(m_device, &allocInfo, &cb));

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    const VkResult result = m_table.vkBeginCommandBuffer(cb, &beginInfo);
    if (result != VK_SUCCESS) {
        m_table.vkFreeCommandBuffers(m_device, m_commandPool, 1, &cb);
        throwVk("vkBeginCommandBuffer", result);
    }
    m_active = std::make_unique<VulkanCommandList>(*this, cb);
    return *m_active;
}

void VulkanDevice::flushUploads() {
    m_buffers.forEachAlive([&](BufferData &d) {
        if (d.desc.memory == rhi::MemoryKind::Upload) {
            vmaFlushAllocation(m_allocator, d.allocation, 0, VK_WHOLE_SIZE);
        }
    });
}

void VulkanDevice::invalidateReadbacks() {
    m_buffers.forEachAlive([&](BufferData &d) {
        if (d.desc.memory == rhi::MemoryKind::Readback) {
            vmaInvalidateAllocation(m_allocator, d.allocation, 0, VK_WHOLE_SIZE);
        }
    });
}

rhi::SubmitTicket VulkanDevice::submit(rhi::CommandList &commands) {
    if (m_active == nullptr || &commands != m_active.get()) {
        throw std::logic_error("submit: command list was not returned by the latest beginCommands()");
    }
    m_active->end();
    flushUploads();

    const uint64_t ticket = ++m_lastTicket;
    VkCommandBufferSubmitInfo cbInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
    cbInfo.commandBuffer = m_active->handle();
    VkSemaphoreSubmitInfo signalInfo{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
    signalInfo.semaphore = m_timeline;
    signalInfo.value = ticket;
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cbInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;

    const VkResult result = m_table.vkQueueSubmit2(m_queue, 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        --m_lastTicket;
        throwVk("vkQueueSubmit2", result);
    }
    m_inFlight.push_back({ticket, std::move(m_active)});
    collect();
    return rhi::SubmitTicket{ticket};
}

void VulkanDevice::wait(rhi::SubmitTicket ticket) {
    if (ticket.value == 0) {
        return;
    }
    if (ticket.value > m_lastTicket) {
        throw std::logic_error("wait: ticket was never submitted");
    }
    VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_timeline;
    waitInfo.pValues = &ticket.value;
    PHOX_VK_CHECK(m_table.vkWaitSemaphores(m_device, &waitInfo, UINT64_MAX));
    invalidateReadbacks();
    collect();
}

void VulkanDevice::waitIdle() {
    PHOX_VK_CHECK(m_table.vkDeviceWaitIdle(m_device));
    invalidateReadbacks();
    collect();
}

} // namespace phox::vulkan
