#include "Renderer.hpp"

#include <iostream>
#include <sstream>
#include <stdexcept>

#include <GLFW/glfw3native.h>

VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
    void *pUserData) {
    (void)messageType;
    (void)pUserData;
    // Select prefix depending on flags passed to the callback
    std::string prefix;

    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT) {
        prefix = "VERBOSE: ";
    } else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
        prefix = "INFO: ";
    } else if (messageSeverity &
               VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        prefix = "WARNING: ";
    } else if (messageSeverity &
               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        prefix = "ERROR: ";
    }

    // Display message to default output (console/logcat)
    std::stringstream debugMessage;
    debugMessage << prefix << "[" << pCallbackData->messageIdNumber << "]["
                 << pCallbackData->pMessageIdName
                 << "] : " << pCallbackData->pMessage;

#if defined(__ANDROID__)
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        LOGE("%s", debugMessage.str().c_str());
    } else {
        LOGD("%s", debugMessage.str().c_str());
    }
#else
    if (messageSeverity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        std::cerr << debugMessage.str() << "\n";
    } else {
        std::cout << debugMessage.str() << "\n";
    }
    fflush(stdout);
#endif

    // The return value of this callback controls whether the Vulkan call that
    // caused the validation message will be aborted or not We return VK_FALSE
    // as we DON'T want Vulkan calls that cause a validation message to abort If
    // you instead want to have calls abort, pass in VK_TRUE and the function
    // will return VK_ERROR_VALIDATION_FAILED_EXT
    return VK_FALSE;
}

namespace phox {

void Renderer::createInstance() {
    VkApplicationInfo appInfo = {};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Phox";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Phox";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    std::vector<const char *> layers;
    if (m_enableValidationLayers) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    std::vector<const char *> extensions;
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions;
    glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    for (int i = 0; i < glfwExtensionCount; i++) {
        extensions.push_back(glfwExtensions[i]);
    }

    if (m_enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    switch (GetPlatformType()) {
    case PlatformType::Linux:
        extensions.push_back("VK_KHR_xlib_surface");
        break;
    case PlatformType::MacOS:
        extensions.push_back("VK_MVK_macos_surface");
    default:
        throw std::runtime_error("Unsupported platform");
        break;
    }

    VkInstanceCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = extensions.size();
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create instance!");
    }
}

void Renderer::setupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo;
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_FLAG_BITS_MAX_ENUM_EXT;
    createInfo.pfnUserCallback = debugUtilsMessengerCallback;

    vkCreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr,
                                   &m_debugMessenger);
}

void Renderer::createSurface(GLFWwindow *window) {
    VkResult result =
        glfwCreateWindowSurface(m_instance, window, nullptr, &m_surface);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create window surface!");
    }
}

void Renderer::createLogicalDevice() {
    auto physDev = Device::PickPhysicalDevice(m_instance, DeviceType::Discrete);

    m_device.initialize(physDev, m_instance, m_surface);
}

void Renderer::initialize(GLFWwindow *window, bool debug) {
    m_enableValidationLayers = debug;

    volkInitialize();

    createInstance();
    volkLoadInstance(m_instance);

    if (m_enableValidationLayers) {
        setupDebugMessenger();
    }

    createSurface(window);
    createLogicalDevice();

    m_initialized = true;
}

void Renderer::cleanup() {

    if (m_device.isInitialized()) {
        m_device.cleanup();
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
    }
    if (m_enableValidationLayers) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
    }

    m_initialized = false;
}

Renderer::~Renderer() {
    if (m_initialized) {
        cleanup();
    }
}

} // namespace phox
