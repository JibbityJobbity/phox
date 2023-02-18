#include "Device.hpp"

#include <stdexcept>
#include <iostream>

namespace phox {

VkPhysicalDevice Device::PickPhysicalDevice(VkInstance instance,
                                            DeviceType type) {
    uint32_t nPhysDev;
    vkEnumeratePhysicalDevices(instance, &nPhysDev, nullptr);
    auto physDevs = new VkPhysicalDevice[nPhysDev];
    vkEnumeratePhysicalDevices(instance, &nPhysDev, physDevs);

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

    delete[] physDevs;
    return out;
}

void Device::initialize(VkPhysicalDevice physicalDevice, VkInstance instance,
                        VkSurfaceKHR surface) {
    VkResult result;
    m_instance = instance;
    m_surface = surface;
    m_physicalDevice = physicalDevice;

    // Pick queue families
    uint32_t nQueueFamilies;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &nQueueFamilies,
                                             nullptr);
    auto *queueFamilies = new VkQueueFamilyProperties[nQueueFamilies];
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &nQueueFamilies,
                                             queueFamilies);
    uint32_t graphicsFamily = -1;
    uint32_t presentFamily = -1;
    VkBool32 presentSupport = false;
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

    m_table.vkGetDeviceQueue(m_device, graphicsFamily, 0, &m_graphicsQueue);
    m_table.vkGetDeviceQueue(m_device, presentFamily, 0, &m_presentQueue);

    m_table.vkDeviceWaitIdle(m_device);


    m_initialized = true;
}

void Device::cleanup() {
    if (!m_initialized) return;

    if (m_device != VK_NULL_HANDLE) {
        m_table.vkQueueWaitIdle(m_graphicsQueue);
        m_table.vkQueueWaitIdle(m_presentQueue);
        m_table.vkDeviceWaitIdle(m_device);
        m_table.vkDestroyDevice(m_device, nullptr);
        m_presentQueue = VK_NULL_HANDLE;
        m_graphicsQueue = VK_NULL_HANDLE;
        m_device = VK_NULL_HANDLE;
    }
    // Device doesn't own these
    m_physicalDevice = VK_NULL_HANDLE;
    m_instance = VK_NULL_HANDLE;
    m_initialized = false;
}

bool Device::isInitialized() { return m_initialized; }

Device::~Device() {
    cleanup();
}

} // namespace phox