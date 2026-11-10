#define VOLK_IMPLEMENTATION
#include "phox/vulkan/Renderer.hpp"

#include "phox/Log.hpp"
#include "phox/RenderGraph.hpp"
#include "phox/RenderView.hpp"
#include "phox/SoftwareRasterizer.hpp"
#include "phox/Window.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Conversions.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/FrameContext.hpp"
#include "phox/vulkan/GraphExecutor.hpp"
#include "phox/vulkan/MeshCache.hpp"
#include "phox/vulkan/GpuScene.hpp"
#include "phox/vulkan/PipelineCache.hpp"
#include "phox/vulkan/ShaderInterface.hpp"
#include "phox/vulkan/ShaderLibrary.hpp"
#include "phox/vulkan/Swapchain.hpp"
#include "phox/vulkan/TextureCache.hpp"
#include "phox/vulkan/TransientPool.hpp"
#include "phox/vulkan/passes/DepthPyramidPass.hpp"
#include "phox/vulkan/passes/ForwardPass.hpp"
#include "phox/vulkan/passes/ReadbackPass.hpp"
#include "phox/vulkan/passes/TonemapPass.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
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

// The forward pass renders lit colour here; the tonemap pass maps it to the display format.
constexpr phox::Format kHdrFormat = phox::Format::RGBA16Float;
// Headless frames are tonemapped into this and read back.
constexpr phox::Format kHeadlessFormat = phox::Format::RGBA8Unorm;

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

        // Phox records with synchronization2 and dynamic rendering (core in Vulkan 1.3), materials index
        // one array of every texture (descriptor indexing), and the GPU writes the draws (indirect count,
        // multi-draw, firstInstance); nearly every 1.2 device has all of these.
        VkPhysicalDeviceProperties versionProps;
        vkGetPhysicalDeviceProperties(device, &versionProps);
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.pNext = &features13;
        VkPhysicalDeviceFeatures2 features{};
        features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features.pNext = &features12;
        vkGetPhysicalDeviceFeatures2(device, &features);
        if (versionProps.apiVersion < PHOX_VULKAN_API_VERSION || !features13.synchronization2 ||
            !features13.dynamicRendering || !phox::vk::hasBindlessTextures(features12) ||
            !phox::vk::hasGpuDrivenDrawing(features.features)) {
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
                                 " (Phox needs Vulkan 1.3 with synchronization2, dynamic rendering, descriptor indexing and indirect count)");
    }
    return best;
}

// What @p view draws: the items in the frustum, their triangles, and the triangles of the meshlets cluster
// LOD picks for them (the task shader's lodSelected, repeated on the CPU from the mesh cache's copies of
// the meshlets). @p view is not culled (GPU-driven rendering culls on the GPU), so this tests the frustum.
phox::FrameStatistics countStatistics(const phox::RenderView &view, const phox::vk::MeshCache &meshes,
                                      float lodErrorPixels) {
    phox::FrameStatistics statistics;
    const float pixelsPerUnit = std::abs(view.proj[1][1]) * 0.5f * static_cast<float>(view.extent.height);
    for (const auto *items : {&view.draws, &view.transparent}) {
        for (const phox::DrawItem &draw : *items) {
            if (phox::outsideFrustum(view.viewProj * draw.model, draw.mesh->bounds())) {
                continue;
            }
            ++statistics.draws;
            statistics.triangles += draw.mesh->triangleCount();
            const phox::vk::MeshCache::GpuMesh &mesh = meshes.get(*draw.mesh);
            if (!meshes.meshlets() || mesh.meshletInfo.empty()) {
                statistics.lodTriangles += draw.mesh->triangleCount();
                continue;
            }
            const phox::vec3 camera = glm::inverse(draw.model) * phox::vec4(view.cameraPosition, 1.0f);
            for (const phox::Meshlet &meshlet : mesh.meshletInfo) {
                if (phox::lodSelected(meshlet, camera, pixelsPerUnit, lodErrorPixels)) {
                    statistics.lodTriangles += meshlet.triangleCount;
                }
            }
        }
    }
    return statistics;
}

// Set 0 of the forward and cull pipelines: the view's FrameUniforms and the frame's material table
// (GpuScene, one entry per instance slot).
VkDescriptorSet writeFrameSet(phox::vk::Device &device, phox::vk::FrameContext &frame, phox::vk::PipelineCache &pipelines,
                              const phox::RenderView &view, const VkDescriptorBufferInfo &materials) {
    phox::vk::FrameUniforms uniforms;
    uniforms.view = view.view;
    uniforms.proj = view.proj;
    uniforms.viewProj = view.viewProj;
    uniforms.cameraPosition = phox::vec4(view.cameraPosition, 1.0f);
    uniforms.lightDirection = phox::vec4(view.lightDirection, view.ambient);
    uniforms.lightColor = phox::vec4(view.lightColor, 1.0f);
    const float width = static_cast<float>(view.extent.width);
    const float height = static_cast<float>(view.extent.height);
    uniforms.resolution = {width, height, 1.0f / width, 1.0f / height};
    std::copy(view.frustumPlanes.begin(), view.frustumPlanes.end(), uniforms.frustumPlanes);
    const phox::vk::UploadArena::Allocation data = frame.arena().uploadUniform(uniforms);

    const VkDescriptorSet set = frame.descriptors().allocate(pipelines.setLayout(phox::SetLayout::Frame));
    const VkDescriptorBufferInfo bufferInfos[2] = {{data.buffer, data.offset, sizeof(uniforms)}, materials};
    VkWriteDescriptorSet writes[2]{};
    for (std::uint32_t i = 0; i < 2; ++i) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = set;
        writes[i].dstBinding = i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = i == 0 ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].pBufferInfo = &bufferInfos[i];
    }
    device.table().vkUpdateDescriptorSets(device.handle(), 2, writes, 0, nullptr);
    return set;
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
        const bool meshShaders = createPipelines();
        m_meshes = std::make_unique<MeshCache>(*m_device, meshShaders, m_config.meshLod, m_config.collectStatistics);
        m_textures = std::make_unique<TextureCache>(*m_device, *m_pipelines);
        bool occlusion = false;
        if (meshShaders && m_config.meshletCulling && m_config.occlusionCulling) {
            if (m_depthFormat == Format::D32Float) {
                occlusion = true;
            } else {
                log::warn("occlusion culling needs a D32 depth buffer; culling meshlets without it");
            }
        }
        m_scene = std::make_unique<GpuScene>(*m_device, meshShaders, occlusion);
        m_occlusion = occlusion;
        if (m_window != nullptr) {
            recreateSwapchain(); // stays empty while the window is minimised; render() retries
        }
        log::info("renderer initialized on {}{}, drawing with {}{}", m_deviceName, m_validation ? " (validation on)" : "",
                  !meshShaders              ? "vertex shaders"
                  : m_occlusion             ? "mesh shaders (frustum, cone and occlusion culling)"
                  : m_config.meshletCulling ? "mesh shaders (frustum and cone culling)"
                                            : "mesh shaders (no meshlet culling)",
                  m_meshes->lod() ? fmt::format(", cluster LOD at {} px", m_config.lodErrorPixels) : std::string());
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
    m_scene.reset();
    m_textures.reset();
    m_meshes.reset();
    m_transients.reset();
    m_pipelines.reset();
    m_shaders.reset();
    for (VkSemaphore semaphore : m_renderFinished) {
        m_device->table().vkDestroySemaphore(m_device->handle(), semaphore, nullptr);
    }
    m_renderFinished.clear();
    m_gpuTimings.clear();
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
    result.meshShaders = !m_useFallback && m_meshes && m_meshes->meshlets();
    result.occlusionCulling = !m_useFallback && m_occlusion;
    result.meshLod = !m_useFallback && m_meshes && m_meshes->lod();
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
    m_pipelines->get(tonemapPipelineDesc(fromVkFormat(m_swapchain->format())));

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
    // The CPU's own work on the frame: from here (past the wait for the GPU) to the submit.
    const auto cpuStart = std::chrono::steady_clock::now();
    if (!frame.timer().timings().empty()) {
        m_gpuTimings = frame.timer().timings();
    }
    m_transients->collect(m_frameNumber);
    m_meshes->collect(m_frameNumber);
    m_textures->collect(m_frameNumber);

    std::uint32_t imageIndex = 0;
    if (windowed && !m_swapchain->acquireNextImage(frame.imageAvailable(), imageIndex)) {
        recreateSwapchain(); // out of date: nothing was acquired, so skip this frame
        return;
    }

    // The camera only: GpuScene keeps the objects, and the GPU culls them.
    const RenderView view = extractCamera(scene, extent);
    if (!windowed) {
        const std::uint64_t bytes = static_cast<std::uint64_t>(extent.width) * extent.height * 4;
        if (!m_readback || m_readback->size() != bytes) {
            m_readback.reset(); // the previous headless frame has completed (render() waits for it)
            m_readback = m_device->makeBuffer(
                {bytes, usageBit(BufferUsage::TransferDst), MemoryLocation::Readback, "readback"});
        }
    }

    const VkCommandBuffer cmd = frame.commandBuffer();
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (table.vkBeginCommandBuffer(cmd, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin the frame command buffer");
    }
    // The GPU's copy of the scene is brought up to date, and everything it uses is resident: what is
    // drawn is decided on the GPU.
    const GpuScene::Frame sceneFrame =
        m_scene->update(scene, view, *m_meshes, *m_textures, frame, cmd, m_frameNumber);
    const VkDescriptorBufferInfo materials{m_scene->materials().handle(), 0, VK_WHOLE_SIZE};
    const ForwardSets sets{writeFrameSet(*m_device, frame, *m_pipelines, view, materials), m_textures->set()};

    // ---- Frame graph -------------------------------------------------------------------------
    //   forward -> hdr -> tonemap -> backbuffer                        [windowed]
    //   forward -> hdr -> tonemap -> display -> readback -> host buffer [headless]
    // "forward" is GPU-driven: clear counters -> cull -> (draw args ->) forward, or with occlusion
    // culling: cull early -> draw args -> forward early -> depth pyramid -> cull late -> draw args ->
    // forward late, with the instance history and meshlet visibility bits imported from the previous
    // frame. Blended objects follow in "forward blend".
    RenderGraph graph(extent);
    GraphExecutor executor(graph, *m_device, *m_transients, frame, m_frameNumber, cmd);
    const ImageHandle hdr = graph.createImage("hdr", ImageDesc{RelativeExtent{1.0f}, kHdrFormat});
    const ImageHandle depth = graph.createImage("depth", ImageDesc{RelativeExtent{1.0f}, m_depthFormat});
    // Read every frame and written where objects changed ("scene update").
    const BufferUsage sceneInitial = sceneFrame.clearInstances ? BufferUsage::Undefined : BufferUsage::StorageRead;
    Buffer &instances = m_scene->instances();
    Buffer &materialBuffer = m_scene->materials();
    const SceneBuffers sceneBuffers{graph.importBuffer("scene instances", BufferDesc{instances.size(), true},
                                                       sceneInitial, BufferUsage::StorageRead),
                                    graph.importBuffer("scene materials", BufferDesc{materialBuffer.size(), true},
                                                       sceneInitial, BufferUsage::StorageRead)};
    executor.bindBuffer(sceneBuffers.instances, instances.handle(), instances.deviceAddress());
    executor.bindBuffer(sceneBuffers.materials, materialBuffer.handle(), materialBuffer.deviceAddress());
    OcclusionState occlusion;
    if (m_occlusion) {
        // Written by the late passes of the previous frame, unless new or restarted (cleared first).
        const BufferUsage initial = sceneFrame.clearState ? BufferUsage::Undefined : BufferUsage::StorageWrite;
        Buffer &history = *m_scene->history();
        Buffer &visibility = *m_scene->visibility();
        occlusion.history = graph.importBuffer("instance visibility", BufferDesc{history.size(), true}, initial,
                                               BufferUsage::StorageWrite);
        occlusion.visibility = graph.importBuffer("meshlet visibility", BufferDesc{visibility.size(), true}, initial,
                                                  BufferUsage::StorageWrite);
        executor.bindBuffer(occlusion.history, history.handle(), history.deviceAddress());
        executor.bindBuffer(occlusion.visibility, visibility.handle(), visibility.deviceAddress());
    }
    const ForwardPassOutput forward =
        addForwardPass(graph, view, sets, sceneFrame, sceneBuffers, hdr, depth, m_meshes->meshlets(), *m_pipelines,
                       MeshletSettings{m_config.meshletCulling, m_config.lodErrorPixels}, m_occlusion ? &occlusion : nullptr);

    ImageHandle backbuffer;
    if (windowed) {
        // Rendered to directly: the tonemap pass is the swapchain image's only writer.
        backbuffer = graph.importImage("backbuffer", ImageDesc{extent, fromVkFormat(m_swapchain->format())},
                                       ImageUsage::Undefined, ImageUsage::Present);
        executor.bindImage(backbuffer, m_swapchain->images().at(imageIndex), m_swapchain->views().at(imageIndex));
        addTonemapPass(graph, forward.color, backbuffer, view.toneMapping, *m_pipelines);
    } else {
        const ImageHandle display = graph.createImage("display", ImageDesc{RelativeExtent{1.0f}, kHeadlessFormat});
        const ImageHandle tonemapped = addTonemapPass(graph, forward.color, display, view.toneMapping, *m_pipelines);
        // The graph makes the copy visible to the host (final state HostRead); the fence orders the read.
        const BufferHandle readback = graph.importBuffer("readback", BufferDesc{m_readback->size()},
                                                         BufferUsage::Undefined, BufferUsage::HostRead);
        executor.bindBuffer(readback, m_readback->handle());
        addReadbackPass(graph, tonemapped, readback);
    }
    graph.compile();
    if (m_config.captureFrameGraph) {
        m_frameGraphDot = graph.toDot();
    }
    graph.execute(executor);

    if (table.vkEndCommandBuffer(cmd) != VK_SUCCESS) {
        throw std::runtime_error("failed to end the frame command buffer");
    }
    frame.arena().flush();
    const double cpuMilliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - cpuStart).count();
    if (m_config.collectStatistics) {
        // Not part of the frame's cost: the renderer itself never lists the objects.
        m_statistics = countStatistics(extractView(scene, extent, false), *m_meshes, m_config.lodErrorPixels);
    }
    m_statistics.cpuMilliseconds = cpuMilliseconds;

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
        waitInfo.stageMask = firstUseStage(graph, backbuffer, m_device->meshShaders());
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
    frame.timer().collect();
    m_gpuTimings = frame.timer().timings();
    m_readback->invalidate();
    const auto *pixels = static_cast<const std::uint8_t *>(m_readback->mapped());
    m_lastFrame.width = extent.width;
    m_lastFrame.height = extent.height;
    m_lastFrame.rgba.assign(pixels, pixels + m_readback->size());
    log::debug("rendered {} object(s): {} instance and {} material update(s)", scene.objectCount(),
               sceneFrame.instanceUpdateCount, sceneFrame.materialUpdateCount);
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
    m_lastFrame = rasterize(extractView(scene, m_config.headlessExtent));
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
    // Non-conformant implementations (MoltenVK on macOS) are only enumerated when asked for.
    if (layerHasExtension(nullptr, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        createInfo.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
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

bool Renderer::createPipelines() {
    // Depth: 32-bit float where it can be an attachment (nearly everywhere), else 24-bit with stencil.
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(m_device->physicalDevice(), VK_FORMAT_D32_SFLOAT, &properties);
    m_depthFormat = (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0
                        ? Format::D32Float
                        : Format::D24UnormS8;

    m_shaders = std::make_unique<ShaderLibrary>(*m_device, kShaderDir);
    m_pipelines = std::make_unique<PipelineCache>(*m_device, *m_shaders);
    // Build the main pipelines now, so missing or broken shaders fail initialization (and select the
    // software fallback) instead of the first frame.
    for (CullMode cull : {CullMode::None, CullMode::Back}) {
        for (bool blend : {false, true}) {
            m_pipelines->get(forwardPipelineDesc(kHdrFormat, m_depthFormat, cull, blend));
        }
    }
    m_pipelines->get(cullPipelineDesc(CullVariant::Vertex));
    m_pipelines->get(sceneUpdatePipelineDesc());
    m_pipelines->get(tonemapPipelineDesc(kHeadlessFormat));
    if (!m_device->meshShaders()) {
        return false;
    }
    // The vertex path still works if the meshlet pipelines cannot be built.
    try {
        for (MeshletPhase phase : {MeshletPhase::Single, MeshletPhase::Early, MeshletPhase::Late}) {
            for (CullMode cull : {CullMode::None, CullMode::Back}) {
                m_pipelines->get(forwardMeshletPipelineDesc(kHdrFormat, m_depthFormat, cull, phase));
            }
        }
        for (CullMode cull : {CullMode::None, CullMode::Back}) {
            m_pipelines->get(forwardMeshletPipelineDesc(kHdrFormat, m_depthFormat, cull, MeshletPhase::Single, true));
        }
        m_pipelines->get(depthPyramidPipelineDesc());
        for (CullVariant variant : {CullVariant::Meshlets, CullVariant::Early, CullVariant::Late}) {
            m_pipelines->get(cullPipelineDesc(variant));
        }
        m_pipelines->get(drawArgsPipelineDesc());
    } catch (const std::exception &e) {
        log::warn("mesh shader pipelines failed, drawing with vertex shaders: {}", e.what());
        return false;
    }
    return true;
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
    m_device = std::make_unique<Device>(m_instance, physicalDevice, m_surface, m_validation, m_config.meshShaders);
    m_deviceName = m_device->properties().deviceName;
}

} // namespace phox::vk
