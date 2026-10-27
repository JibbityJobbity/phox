#define VOLK_IMPLEMENTATION
#include "phox/vulkan/Renderer.hpp"

#include "phox/Log.hpp"
#include "phox/RenderGraph.hpp"
#include "phox/Window.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Conversions.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/GraphExecutor.hpp"
#include "phox/vulkan/Swapchain.hpp"
#include "phox/vulkan/TransientPool.hpp"
#include "phox/vulkan/pipeline/GraphicsPipeline.hpp"
#include "phox/vulkan/pipeline/Shader.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef PHOX_SHADER_DIR
#define PHOX_SHADER_DIR "build/shaders"
#endif

namespace {

const std::filesystem::path kShaderDir = PHOX_SHADER_DIR;

struct SimpleVertex {
    float position[3];
    float normal[3];
    float texCoord[2];
};

struct CameraUniformBuffer {
    std::array<float, 16> viewProjection{};
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugUtilsMessengerCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                           VkDebugUtilsMessageTypeFlagsEXT type,
                                                           const VkDebugUtilsMessengerCallbackDataEXT *data,
                                                           void *userData) {
    auto *counters = static_cast<phox::vk::ValidationCounters *>(userData);
    const char *id = data->pMessageIdName != nullptr ? data->pMessageIdName : "";
    // GENERAL messages come from the loader (e.g. an ICD on the system that fails to load), not from
    // validating this program, so they are neither counted nor reported above debug level.
    const bool fromValidation =
        (type & (VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT)) != 0;
    if (!fromValidation) {
        if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            phox::log::debug("[vulkan] {}: {}", id, data->pMessage); // loader info chatter is dropped
        }
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        counters->errors++;
        phox::log::error("[vulkan] {}: {}", id, data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        counters->warnings++;
        phox::log::warn("[vulkan] {}: {}", id, data->pMessage);
    } else {
        phox::log::debug("[vulkan] {}: {}", id, data->pMessage);
    }
    return VK_FALSE; // never abort the call that triggered the message
}

VkDebugUtilsMessengerCreateInfoEXT messengerCreateInfo(phox::vk::ValidationCounters *counters) {
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    if (phox::log::level() <= phox::log::Level::Debug) {
        info.messageSeverity |= VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT; // e.g. the layer's list of enabled checks
    }
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugUtilsMessengerCallback;
    info.pUserData = counters;
    return info;
}

bool hasInstanceLayer(const char *name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::any_of(layers.begin(), layers.end(),
                       [&](const VkLayerProperties &l) { return std::strcmp(l.layerName, name) == 0; });
}

bool layerHasExtension(const char *layer, const char *extension) {
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(layer, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(layer, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(),
                       [&](const VkExtensionProperties &e) { return std::strcmp(e.extensionName, extension) == 0; });
}

constexpr const char *kValidationLayer = "VK_LAYER_KHRONOS_validation";

std::array<uint8_t, 4> makeColor(const std::array<float, 4> &albedo) {
    auto clamp = [](float value) {
        return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
    };

    return {clamp(albedo[0]), clamp(albedo[1]), clamp(albedo[2]), clamp(albedo[3])};
}

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

// Picks the best device that can draw (and, with a surface, present). With @p preferred set, only
// devices whose name contains it (case-insensitive) are considered.
VkPhysicalDevice pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface, const std::string &preferred) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;
    for (VkPhysicalDevice device : devices) {
        if (!preferred.empty()) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);
            if (toLower(props.deviceName).find(toLower(preferred)) == std::string::npos) {
                continue;
            }
        }
        uint32_t nFamilies = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &nFamilies, nullptr);
        std::vector<VkQueueFamilyProperties> families(nFamilies);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &nFamilies, families.data());

        bool canDraw = false;
        bool canPresent = surface == VK_NULL_HANDLE;
        for (uint32_t i = 0; i < nFamilies; i++) {
            canDraw = canDraw || (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            if (surface != VK_NULL_HANDLE) {
                VkBool32 supported = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &supported);
                canPresent = canPresent || supported == VK_TRUE;
            }
        }
        if (!canDraw || !canPresent) {
            continue;
        }

        // Phox records with synchronization2 and dynamic rendering (core in Vulkan 1.3).
        VkPhysicalDeviceProperties versionProps;
        vkGetPhysicalDeviceProperties(device, &versionProps);
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features13;
        vkGetPhysicalDeviceFeatures2(device, &features);
        if (versionProps.apiVersion < PHOX_VULKAN_API_VERSION || !features13.synchronization2 ||
            !features13.dynamicRendering) {
            continue;
        }

        if (surface != VK_NULL_HANDLE) {
            uint32_t nExtensions = 0;
            vkEnumerateDeviceExtensionProperties(device, nullptr, &nExtensions, nullptr);
            std::vector<VkExtensionProperties> extensions(nExtensions);
            vkEnumerateDeviceExtensionProperties(device, nullptr, &nExtensions, extensions.data());
            const bool hasSwapchain = std::any_of(extensions.begin(), extensions.end(), [](const auto &e) {
                return std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
            });
            if (!hasSwapchain) {
                continue;
            }
        }

        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(device, &props);
        int score = 1;
        switch (props.deviceType) {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   score = 1000; break;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: score = 100; break;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    score = 50; break;
        case VK_PHYSICAL_DEVICE_TYPE_CPU:            score = 10; break;
        default: break;
        }
        if (score > bestScore) {
            best = device;
            bestScore = score;
        }
    }

    if (best == VK_NULL_HANDLE) {
        throw std::runtime_error((preferred.empty() ? std::string("No suitable Vulkan device found")
                                                    : "No suitable Vulkan device matching '" + preferred + "'") +
                                 " (Phox needs Vulkan 1.3 with synchronization2 and dynamic rendering)");
    }
    return best;
}

} // namespace

namespace phox::vk {

Renderer::Renderer() = default;

Renderer::~Renderer() { releaseVulkan(); }

void Renderer::initialize(const RendererConfig &config) {
    m_config = config;
    m_window = config.window;
    if (m_config.preferredDevice.empty()) {
        if (const char *env = std::getenv("PHOX_DEVICE")) {
            m_config.preferredDevice = env;
        }
    }
    if (m_config.debug && std::getenv("PHOX_LOG") == nullptr) {
        log::setLevel(log::Level::Debug); // an explicit PHOX_LOG wins
    }
    m_useFallback = false;

    try {
        if (volkInitialize() != VK_SUCCESS) {
            throw std::runtime_error("Volk is unavailable in this environment");
        }
        createInstance();
        volkLoadInstance(m_instance);
        if (m_validation) {
            setupDebugMessenger();
        }
        createSurface();
        createDevice();
        for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
            m_frames[i] = std::make_unique<FrameContext>(*m_device, i);
        }
        m_transients = std::make_unique<TransientPool>(*m_device);
        createPipeline();
        if (m_window != nullptr) {
            recreateSwapchain(); // stays empty while the window is minimised; render() retries
        }
        log::info("renderer initialized on {}{}", m_deviceName, m_validation ? " (validation on)" : "");
    } catch (const std::exception &e) {
        log::warn("Vulkan initialization failed, using the software fallback renderer: {}", e.what());
        releaseVulkan();
        m_useFallback = true;
    }
    m_initialized = true;
}

void Renderer::cleanup() {
    releaseVulkan();
    m_initialized = false;
    m_useFallback = false;
}

void Renderer::releaseVulkan() {
    if (m_device) {
        m_device->waitIdle();
    }
    // Children before the device; everything below is safe to call on null members.
    m_readback.reset();
    for (auto &frame : m_frames) {
        frame.reset();
    }
    m_transients.reset();
    m_pipeline.reset();
    m_fragShader.reset();
    m_vertShader.reset();
    for (VkSemaphore semaphore : m_renderFinished) {
        m_device->table().vkDestroySemaphore(m_device->handle(), semaphore, nullptr);
    }
    m_renderFinished.clear();
    m_swapchain.reset();
    m_windowSize = {};
    m_device.reset();
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_debugMessenger != VK_NULL_HANDLE) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }
}

RendererInfo Renderer::info() const {
    RendererInfo result;
    if (m_initialized) {
        result.backend = m_useFallback ? RendererBackend::Software : RendererBackend::Vulkan;
    }
    result.deviceName = m_useFallback ? "software rasterizer" : m_deviceName;
    result.validation = m_validation;
    result.validationErrors = m_validationCounters.errors.load();
    result.validationWarnings = m_validationCounters.warnings.load();
    return result;
}

bool Renderer::ensureSwapchain() {
    int width = 0, height = 0;
    m_window->framebufferSize(width, height);
    if (width <= 0 || height <= 0) {
        return false; // minimised
    }
    if (!m_swapchain || static_cast<std::uint32_t>(width) != m_windowSize.width ||
        static_cast<std::uint32_t>(height) != m_windowSize.height) {
        recreateSwapchain();
    }
    return m_swapchain != nullptr;
}

void Renderer::recreateSwapchain() {
    int width = 0, height = 0;
    m_window->framebufferSize(width, height);
    if (width <= 0 || height <= 0) {
        return; // minimised; keep what we have until the window has an area again
    }
    m_device->waitIdle();
    // Built from the old swapchain, which is destroyed only once the new one exists.
    m_swapchain = std::make_unique<Swapchain>(*m_device, m_surface, static_cast<std::uint32_t>(width),
                                              static_cast<std::uint32_t>(height), m_swapchain.get());
    m_windowSize = {static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    while (m_renderFinished.size() < m_swapchain->images().size()) {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (m_device->table().vkCreateSemaphore(m_device->handle(), &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            throw std::runtime_error("failed to create a render-finished semaphore");
        }
        m_device->setObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<std::uint64_t>(semaphore),
                                "render finished " + std::to_string(m_renderFinished.size()));
        m_renderFinished.push_back(semaphore);
    }
    log::debug("swapchain {}x{} with {} images", m_swapchain->extent().width, m_swapchain->extent().height,
               m_swapchain->images().size());
}

void Renderer::render(const Scene &scene) {
    if (!m_initialized) {
        return;
    }
    if (m_useFallback) {
        renderFallback(scene);
        return;
    }

    const bool windowed = m_window != nullptr;
    if (windowed && !ensureSwapchain()) {
        return;
    }
    const Extent2D extent = windowed ? Extent2D{m_swapchain->extent().width, m_swapchain->extent().height}
                                     : m_config.headlessExtent;
    const VkDevice dev = m_device->handle();
    const VolkDeviceTable &table = m_device->table();

    // Wait until the GPU is done with the last frame that used this slot, then reuse its resources.
    FrameContext &frame = *m_frames[m_frameNumber % kFramesInFlight];
    frame.begin();
    m_transients->collect(m_frameNumber);

    std::uint32_t imageIndex = 0;
    if (windowed && !m_swapchain->acquireNextImage(frame.imageAvailable(), imageIndex)) {
        recreateSwapchain(); // out of date: nothing was acquired, so skip this frame
        return;
    }

    // Flatten the scene into one vertex stream. Material albedo comes from the first object (a single
    // push constant is used for the whole draw).
    std::vector<SimpleVertex> vertices;
    for (const auto &object : scene.objects()) {
        for (const auto &triangle : object->mesh().triangles()) {
            for (const MeshVertex *v : {&triangle.a, &triangle.b, &triangle.c}) {
                vertices.push_back({{v->x, v->y, v->z}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}});
            }
        }
    }
    const std::array<float, 4> albedo =
        scene.objects().empty() ? std::array<float, 4>{} : scene.objects().front()->material().albedo;

    // Per-frame data lives in the frame's upload arena, recycled once the GPU has finished with it.
    UploadArena &arena = frame.arena();
    UploadArena::Allocation vertexData{};
    if (!vertices.empty()) {
        vertexData = arena.upload(vertices.data(), sizeof(SimpleVertex) * vertices.size());
    }
    CameraUniformBuffer camera{};
    // Scene [-0.5, 0.5] fills the target, +y is up (matches renderFallback).
    camera.viewProjection = {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
    const UploadArena::Allocation cameraData = arena.uploadUniform(camera);

    const VkDescriptorSet descriptorSet = frame.descriptors().allocate(m_pipeline->descriptorSetLayout());
    VkDescriptorBufferInfo bufferInfo{cameraData.buffer, cameraData.offset, sizeof(CameraUniformBuffer)};
    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = descriptorSet;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.pBufferInfo = &bufferInfo;
    table.vkUpdateDescriptorSets(dev, 1, &write, 0, nullptr);

    if (!windowed) {
        const std::uint64_t bytes = static_cast<std::uint64_t>(extent.width) * extent.height * 4;
        if (!m_readback || m_readback->size() != bytes) {
            m_readback.reset(); // the previous headless frame has completed (render() waits for it)
            BufferCreateInfo info;
            info.size = bytes;
            info.usage = usageBit(BufferUsage::TransferDst);
            info.memory = MemoryLocation::Readback;
            info.name = "readback";
            m_readback = m_device->makeBuffer(info);
        }
    }

    const VkCommandBuffer cmd = frame.commandBuffer();
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (table.vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin the frame command buffer");
    }

    // ---- Frame graph -------------------------------------------------------------------------
    //   scene -> color -> present  (blit into the swapchain image)   [windowed]
    //   scene -> color -> readback (copy into a host-visible buffer) [headless]
    RenderGraph graph(extent);
    GraphExecutor executor(graph, *m_device, *m_transients, frame, m_frameNumber, cmd);

    struct SceneOutput {
        ImageHandle color;
    };
    const VkPipelineLayout pipelineLayout = m_pipeline->layout();
    const VkPipeline pipeline = m_pipeline->handle();
    const SceneOutput &sceneOutput = graph.addPass<SceneOutput>(
        "scene", PassType::Graphics,
        [&](PassBuilder &builder, SceneOutput &out) {
            const ImageHandle color = graph.createImage("color", ImageDesc{RelativeExtent{1.0f}, Format::RGBA8Unorm});
            out.color = builder.colorAttachment(color, LoadOp::Clear, StoreOp::Store, {0.1f, 0.2f, 0.3f, 1.0f});
        },
        [&](const SceneOutput &, phox::PassContext &) {
            if (vertices.empty()) {
                return;
            }
            table.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            table.vkCmdBindVertexBuffers(cmd, 0, 1, &vertexData.buffer, &vertexData.offset);
            table.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0,
                                          nullptr);
            table.vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(albedo), albedo.data());
            table.vkCmdDraw(cmd, static_cast<std::uint32_t>(vertices.size()), 1, 0, 0);
        });

    ImageHandle backbuffer;
    if (windowed) {
        const VkExtent2D swapExtent = m_swapchain->extent();
        Format swapFormat = fromVkFormat(m_swapchain->format());
        backbuffer = graph.importImage("backbuffer",
                                       ImageDesc{Extent2D{swapExtent.width, swapExtent.height},
                                                 swapFormat == Format::Undefined ? Format::BGRA8Unorm : swapFormat},
                                       ImageUsage::Undefined, ImageUsage::Present);
        executor.bindImage(backbuffer, m_swapchain->images().at(imageIndex));
        graph.addPass(
            "present", PassType::Transfer,
            [&](PassBuilder &builder) {
                builder.read(sceneOutput.color, ImageUsage::TransferSrc);
                builder.write(backbuffer, ImageUsage::TransferDst);
            },
            [&, color = sceneOutput.color](phox::PassContext &context) {
                auto &ctx = PassContext::from(context);
                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blit.srcOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blit.dstOffsets[1] = {static_cast<int32_t>(swapExtent.width), static_cast<int32_t>(swapExtent.height), 1};
                table.vkCmdBlitImage(ctx.cmd(), ctx.image(color), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                     ctx.image(backbuffer), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                                     VK_FILTER_NEAREST);
            });
    } else {
        // The graph makes the copy visible to the host (final state HostRead); the fence orders the read.
        const BufferHandle readback = graph.importBuffer("readback", BufferDesc{m_readback->size()},
                                                         BufferUsage::Undefined, BufferUsage::HostRead);
        executor.bindBuffer(readback, m_readback->handle());
        graph.addPass(
            "readback", PassType::Transfer,
            [&](PassBuilder &builder) {
                builder.read(sceneOutput.color, ImageUsage::TransferSrc);
                builder.write(readback, BufferUsage::TransferDst);
            },
            [&, color = sceneOutput.color](phox::PassContext &context) {
                auto &ctx = PassContext::from(context);
                VkBufferImageCopy region{};
                region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                region.imageExtent = {extent.width, extent.height, 1};
                table.vkCmdCopyImageToBuffer(ctx.cmd(), ctx.image(color), VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                             ctx.buffer(readback), 1, &region);
            });
    }

    graph.compile();
    graph.execute(executor);

    if (table.vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("failed to end the frame command buffer");
    }
    arena.flush();

    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = cmd;
    VkSemaphoreSubmitInfo waitInfo{};
    VkSemaphoreSubmitInfo signalInfo{};
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    if (windowed) {
        // Block only the stage that first touches the swapchain image, as reported by the graph.
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfo.semaphore = frame.imageAvailable();
        waitInfo.stageMask = firstUseStage(graph, backbuffer);
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfo.semaphore = m_renderFinished[imageIndex];
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &waitInfo;
        submitInfo.signalSemaphoreInfoCount = 1;
        submitInfo.pSignalSemaphoreInfos = &signalInfo;
    }
    // Reset only now: had we returned early with the fence unsignalled, the next begin() would hang.
    const VkFence fence = frame.fence();
    table.vkResetFences(dev, 1, &fence);
    if (table.vkQueueSubmit2(m_device->graphicsQueue(), 1, &submitInfo, fence) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit the frame");
    }
    ++m_frameNumber;

    if (windowed) {
        if (!m_swapchain->present(imageIndex, m_renderFinished[imageIndex])) {
            recreateSwapchain(); // out of date or suboptimal
        }
        return;
    }

    // Headless frames are read back straight away, so they do not overlap.
    if (table.vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        throw std::runtime_error("waiting for the headless frame failed");
    }
    m_readback->invalidate();
    const auto *pixels = static_cast<const std::uint8_t *>(m_readback->mapped());
    m_lastFrame.width = extent.width;
    m_lastFrame.height = extent.height;
    m_lastFrame.rgba.assign(pixels, pixels + m_readback->size());
    log::debug("rendered {} object(s), {} vertices", scene.objectCount(), vertices.size());
}

void Renderer::renderFallback(const Scene &scene) {
    if (m_window != nullptr) {
        static bool warned = false;
        if (!warned) {
            log::warn("the software fallback cannot present to a window; frames are not shown");
            warned = true;
        }
        return;
    }
    const uint32_t width = m_config.headlessExtent.width;
    const uint32_t height = m_config.headlessExtent.height;
    // Same clear colour as the GPU path (0.1, 0.2, 0.3) so both backends produce the same image.
    std::vector<std::array<uint8_t, 4>> pixels(static_cast<std::size_t>(width) * height, {26, 51, 76, 0xff});

    const auto toScreen = [&](float x, float y) {
        return std::array<float, 2>{
            (x + 0.5f) * static_cast<float>(width),
            (1.0f - (y + 0.5f)) * static_cast<float>(height),
        };
    };

    const auto rasterizeTriangle = [&](const Triangle &triangle, const std::array<float, 4> &albedo) {
        const auto p0 = toScreen(triangle.a.x, triangle.a.y);
        const auto p1 = toScreen(triangle.b.x, triangle.b.y);
        const auto p2 = toScreen(triangle.c.x, triangle.c.y);

        const float det = (p1[1] - p2[1]) * (p0[0] - p2[0]) + (p2[0] - p1[0]) * (p0[1] - p2[1]);
        if (std::abs(det) < 1e-6f) {
            return;
        }

        const auto color = makeColor(albedo);
        const int minX = std::max(0, static_cast<int>(std::floor(std::min({p0[0], p1[0], p2[0]}))));
        const int maxX = std::min(static_cast<int>(width) - 1, static_cast<int>(std::ceil(std::max({p0[0], p1[0], p2[0]}))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({p0[1], p1[1], p2[1]}))));
        const int maxY = std::min(static_cast<int>(height) - 1, static_cast<int>(std::ceil(std::max({p0[1], p1[1], p2[1]}))));

        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;

                const float l0 = ((p1[1] - p2[1]) * (px - p2[0]) + (p2[0] - p1[0]) * (py - p2[1])) / det;
                const float l1 = ((p2[1] - p0[1]) * (px - p2[0]) + (p0[0] - p2[0]) * (py - p2[1])) / det;
                const float l2 = 1.0f - l0 - l1;

                if (l0 >= 0.0f && l1 >= 0.0f && l2 >= 0.0f) {
                    const size_t idx = static_cast<size_t>(y) * width + static_cast<size_t>(x);
                    pixels[idx] = color;
                }
            }
        }
    };

    for (const auto &object : scene.objects()) {
        const auto &mesh = object->mesh();
        for (const auto &triangle : mesh.triangles()) {
            rasterizeTriangle(triangle, object->material().albedo);
        }
    }

    m_lastFrame.width = width;
    m_lastFrame.height = height;
    m_lastFrame.rgba.resize(pixels.size() * 4);
    std::memcpy(m_lastFrame.rgba.data(), pixels.data(), m_lastFrame.rgba.size());
}

void Renderer::createInstance() {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Phox";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Phox";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = PHOX_VULKAN_API_VERSION;

    m_validation = m_config.debug;
    if (m_validation && !hasInstanceLayer(kValidationLayer)) {
        log::warn("validation requested but {} is not installed; continuing without it", kValidationLayer);
        m_validation = false;
    }

    std::vector<const char *> layers;
    std::vector<const char *> extensions;
    const void *next = nullptr;

    // Chained into instance creation so messages from vkCreateInstance itself are caught too.
    VkDebugUtilsMessengerCreateInfoEXT messengerInfo = messengerCreateInfo(&m_validationCounters);
    const VkBool32 enabled = VK_TRUE;
    const VkLayerSettingEXT syncValidation{kValidationLayer, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
                                           &enabled};
    VkLayerSettingsCreateInfoEXT layerSettings{};
    if (m_validation) {
        layers.push_back(kValidationLayer);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        next = &messengerInfo;
        if (layerHasExtension(kValidationLayer, VK_EXT_LAYER_SETTINGS_EXTENSION_NAME)) {
            extensions.push_back(VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
            layerSettings.sType = VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT;
            layerSettings.pNext = next;
            layerSettings.settingCount = 1;
            layerSettings.pSettings = &syncValidation;
            next = &layerSettings;
        } else {
            log::warn("the validation layer lacks {}; synchronization validation stays off",
                      VK_EXT_LAYER_SETTINGS_EXTENSION_NAME);
        }
    }

    // Surface extensions come from the window; headless needs none.
    std::vector<std::string> windowExtensions;
    if (m_window != nullptr) {
        windowExtensions = m_window->requiredVulkanExtensions();
        for (const auto &name : windowExtensions) {
            extensions.push_back(name.c_str());
        }
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pNext = next;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    const VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("vkCreateInstance failed (VkResult " + std::to_string(result) + ")");
    }
}

void Renderer::setupDebugMessenger() {
    const VkDebugUtilsMessengerCreateInfoEXT createInfo = messengerCreateInfo(&m_validationCounters);
    if (vkCreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr, &m_debugMessenger) != VK_SUCCESS) {
        log::warn("failed to create the debug messenger");
    }
}

void Renderer::createPipeline() {
    m_vertShader = Shader::fromFile(*m_device, (kShaderDir / "basic.vert.spv").string(), ShaderType::VERTEX);
    m_fragShader = Shader::fromFile(*m_device, (kShaderDir / "basic.frag.spv").string(), ShaderType::FRAGMENT);
    m_pipeline = m_device->createPipeline({m_vertShader.get(), m_fragShader.get()});
}

void Renderer::createSurface() {
    m_surface = VK_NULL_HANDLE;
    if (m_window == nullptr) {
        return;
    }
    std::uint64_t raw = 0;
    if (!m_window->createVulkanSurface(m_instance, &raw)) {
        throw std::runtime_error("Failed to create window surface");
    }
    std::memcpy(&m_surface, &raw, sizeof(m_surface));
}

void Renderer::createDevice() {
    const VkPhysicalDevice physicalDevice = pickPhysicalDevice(m_instance, m_surface, m_config.preferredDevice);
    // Object names need VK_EXT_debug_utils, which is enabled alongside validation.
    m_device = std::make_unique<Device>(m_instance, physicalDevice, m_surface, m_validation);
    m_deviceName = m_device->properties().deviceName;
}

} // namespace phox::vk
