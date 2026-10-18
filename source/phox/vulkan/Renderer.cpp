#define VOLK_IMPLEMENTATION
#include "phox/vulkan/Renderer.hpp"

#include "phox/RenderGraph.hpp"
#include "phox/Window.hpp"
#include "phox/vulkan/GraphExecutor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>

#ifndef PHOX_SHADER_DIR
#define PHOX_SHADER_DIR "build/shaders"
#endif

namespace {

const std::filesystem::path kShaderDir = PHOX_SHADER_DIR;
const std::filesystem::path kOutputPath = "build/rendered_triangle.ppm";

struct SimpleVertex {
    float position[3];
    float normal[3];
    float texCoord[2];
};

struct CameraUniformBuffer {
    std::array<float, 16> viewProjection{};
};

// Owns the per-frame Vulkan objects that are not graph resources (framebuffer, descriptors, command pool,
// semaphores, readback buffer), so an
// exception anywhere in render() cannot leak them. Handles are destroyed in
// reverse dependency order; destroying a null handle is a no-op.
struct FrameResources {
    explicit FrameResources(phox::vk::Device &d) : device(d) {}
    FrameResources(const FrameResources &) = delete;
    FrameResources &operator=(const FrameResources &) = delete;

    ~FrameResources() {
        const VkDevice dev = device.handle();
        const auto &t = device.table();
        if (dev == VK_NULL_HANDLE) {
            return;
        }
        t.vkDeviceWaitIdle(dev);
        if (stagingBuffer != VK_NULL_HANDLE || stagingAllocation != VK_NULL_HANDLE) {
            vmaDestroyBuffer(device.allocator(), stagingBuffer, stagingAllocation);
        }
        t.vkDestroySemaphore(dev, renderFinished, nullptr);
        t.vkDestroySemaphore(dev, imageAvailable, nullptr);
        t.vkDestroyCommandPool(dev, commandPool, nullptr);
        t.vkDestroyFramebuffer(dev, framebuffer, nullptr);
        t.vkDestroyDescriptorPool(dev, descriptorPool, nullptr);
    }

    phox::vk::Device &device;
    VkFramebuffer framebuffer = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = VK_NULL_HANDLE;
    VkSemaphore imageAvailable = VK_NULL_HANDLE;
    VkSemaphore renderFinished = VK_NULL_HANDLE;
};

// Waits for the GPU when it goes out of scope. Declared after the per-frame buffers it protects, so on
// every exit path (including exceptions) it runs before they are destroyed.
struct DeviceIdleGuard {
    phox::vk::Device &device;
    ~DeviceIdleGuard() {
        if (device.handle() != VK_NULL_HANDLE) {
            device.table().vkDeviceWaitIdle(device.handle());
        }
    }
};

std::array<uint8_t, 4> makeColor(const std::array<float, 4> &albedo) {
    auto clamp = [](float value) {
        return static_cast<uint8_t>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
    };

    return {clamp(albedo[0]), clamp(albedo[1]), clamp(albedo[2]), clamp(albedo[3])};
}

void writePpm(const std::filesystem::path &path, uint32_t width, uint32_t height,
              const std::vector<std::array<uint8_t, 4>> &pixels) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output << "P6\n" << width << " " << height << "\n255\n";
    for (const auto &pixel : pixels) {
        output.write(reinterpret_cast<const char *>(pixel.data()), 3);
    }
}



// Picks the best device that can draw (and, with a surface, present and use a swapchain).
VkPhysicalDevice pickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface) {
    uint32_t count = 0;
    vkEnumeratePhysicalDevices(instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(instance, &count, devices.data());

    VkPhysicalDevice best = VK_NULL_HANDLE;
    int bestScore = -1;
    for (VkPhysicalDevice device : devices) {
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
        throw std::runtime_error("No suitable Vulkan device found");
    }
    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(best, &props);
    std::cerr << "[device] using " << props.deviceName << std::endl;
    return best;
}

} // namespace

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

namespace vk {

Renderer::Renderer() { }

void Renderer::initialize(bool debug, Window *window) {
    m_window = window;
    std::cerr << "[init] starting renderer initialization" << std::endl;
    m_enableValidationLayers = debug;
    m_useFallback = false;

    try {
        std::cerr << "[init] initializing Vulkan loader" << std::endl;
        if (volkInitialize() != VK_SUCCESS) {
            throw std::runtime_error("Volk is unavailable in this environment");
        }

        createInstance();
        std::cerr << "[init] instance created" << std::endl;
        volkLoadInstance(m_instance);

        if (m_enableValidationLayers) {
            std::cerr << "[init] setting up debug messenger" << std::endl;
            setupDebugMessenger();
        }

        std::cerr << "[init] creating surface" << std::endl;
        createSurface();
        std::cerr << "[init] creating logical device" << std::endl;
        createLogicalDevice();

        if (m_device && m_device->isInitialized()) {
            std::cerr << "[init] initializing swapchain and pipeline" << std::endl;
            createSwapchain();
            createPipeline();
        }

        m_initialized = true;
        std::cerr << "[init] renderer initialized" << std::endl;
    } catch (const std::exception &e) {
        std::cerr << "[init] Vulkan initialization failed, using software fallback renderer: "
                  << e.what() << std::endl;
        m_useFallback = true;
        m_initialized = true;
    }
}

void Renderer::cleanup() {
    // Children first: swapchain/layout need a live device to destroy themselves.
    if (m_device && m_device->isInitialized()) {
        m_device->table().vkDeviceWaitIdle(m_device->handle());
    }
    m_pipeline.reset();
    m_fragShader.reset();
    m_vertShader.reset();
    m_swapchain.reset();
    if (m_device) {
        m_device->cleanup();
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
    if (m_debugMessenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT != nullptr) {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_debugMessenger, nullptr);
        m_debugMessenger = VK_NULL_HANDLE;
    }
    if (m_instance != VK_NULL_HANDLE) {
        vkDestroyInstance(m_instance, nullptr);
        m_instance = VK_NULL_HANDLE;
    }

    m_initialized = false;
}

void Renderer::render(const Scene &scene) {
    if (!m_initialized) {
        return;
    }

    if (m_useFallback) {
        renderFallback(scene);
        return;
    }

    if (!m_device || !m_device->isInitialized() || !m_pipeline) {
        renderFallback(scene);
        return;
    }

    std::size_t totalTriangles = 0;
    for (const auto &object : scene.objects()) {
        totalTriangles += object->mesh().triangleCount();
    }

    if (totalTriangles == 0) {
        renderFallback(scene);
        return;
    }

    const bool windowed = m_window != nullptr && m_surface != VK_NULL_HANDLE;
    if (windowed) {
        int windowWidth = 0, windowHeight = 0;
        m_window->framebufferSize(windowWidth, windowHeight);
        if (windowWidth <= 0 || windowHeight <= 0) {
            return; // minimised
        }
        if (!m_swapchain || windowWidth != m_windowWidth || windowHeight != m_windowHeight) {
            recreateSwapchain();
        }
        if (!m_swapchain || !m_swapchain->valid()) {
            return;
        }
    }
    // Windowed frames match the swapchain; headless frames are a fixed size.
    const uint32_t width = windowed ? m_swapchain->extent().width : 640;
    const uint32_t height = windowed ? m_swapchain->extent().height : 480;
    if (m_enableValidationLayers) std::cout << "[render] starting triangle render" << std::endl;

    // Flatten the scene into one vertex stream. Material albedo comes from the
    // first object (a single push constant is used for the whole draw).
    std::vector<SimpleVertex> vertices;
    vertices.reserve(totalTriangles * 3);
    std::array<float, 4> albedo = scene.objects().front()->material().albedo;
    for (const auto &object : scene.objects()) {
        for (const auto &triangle : object->mesh().triangles()) {
            for (const MeshVertex *v : {&triangle.a, &triangle.b, &triangle.c}) {
                vertices.push_back({{v->x, v->y, v->z}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}});
            }
        }
    }

    // Declared before `res` so they are destroyed after it: the framebuffer (in `res`) must go before
    // the graph-owned image view it references.
    GraphExecutor executor(*m_device);
    FrameResources res(*m_device);
    const VkRenderPass renderPass = m_pipeline->renderPass();
    const VkPipelineLayout pipelineLayout = m_pipeline->layout();
    const VkDescriptorSetLayout descriptorSetLayout = m_pipeline->descriptorSetLayout();
    const VkPipeline pipeline = m_pipeline->handle();
    auto &framebuffer = res.framebuffer;
    auto &descriptorPool = res.descriptorPool;
    auto &commandPool = res.commandPool;

    // Windowed: get the backbuffer first. If the swapchain is stale, rebuild it and skip this frame.
    uint32_t imageIndex = 0;
    if (windowed) {
        VkSemaphoreCreateInfo semaphoreInfo{};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        const auto &table = m_device->table();
        if (table.vkCreateSemaphore(m_device->handle(), &semaphoreInfo, nullptr, &res.imageAvailable) != VK_SUCCESS ||
            table.vkCreateSemaphore(m_device->handle(), &semaphoreInfo, nullptr, &res.renderFinished) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create semaphores");
        }
        if (!m_swapchain->acquireNextImage(res.imageAvailable, imageIndex)) {
            recreateSwapchain();
            return;
        }
    }

    if (m_enableValidationLayers) std::cout << "[render] allocating vertex buffer" << std::endl;
    auto vertexBuffer = m_device->allocateBuffer(sizeof(SimpleVertex) * vertices.size(), MemoryLocation::Host);
    auto* vertexBufferVk = dynamic_cast<vk::Buffer*>(vertexBuffer.get());
    if (vertexBufferVk == nullptr) {
        throw std::runtime_error("Failed to create Vulkan vertex buffer");
    }
    if (m_enableValidationLayers) std::cout << "[render] copying vertex data" << std::endl;
    std::memcpy(vertexBufferVk->map(), vertices.data(), sizeof(SimpleVertex) * vertices.size());
    vertexBufferVk->unmap();

    CameraUniformBuffer cameraUniform{};
    // Matches renderFallback's mapping: scene [-0.5, 0.5] fills the target, +y is up.
    cameraUniform.viewProjection = {
        2.0f, 0.0f, 0.0f, 0.0f,
        0.0f, -2.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };

    if (m_enableValidationLayers) std::cout << "[render] allocating uniform buffer" << std::endl;
    auto uniformBuffer = m_device->allocateBuffer(sizeof(CameraUniformBuffer), MemoryLocation::Host);
    auto* uniformBufferVk = dynamic_cast<vk::Buffer*>(uniformBuffer.get());
    if (uniformBufferVk == nullptr) {
        throw std::runtime_error("Failed to create Vulkan uniform buffer");
    }
    if (m_enableValidationLayers) std::cout << "[render] copying uniform data" << std::endl;
    std::memcpy(uniformBufferVk->map(), &cameraUniform, sizeof(CameraUniformBuffer));
    uniformBufferVk->unmap();
    DeviceIdleGuard idleGuard{*m_device};

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.maxSets = 1;
    descriptorPoolInfo.poolSizeCount = 1;
    descriptorPoolInfo.pPoolSizes = &poolSize;

    if (m_device->table().vkCreateDescriptorPool(m_device->handle(), &descriptorPoolInfo, nullptr, &descriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create descriptor pool");
    }

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &descriptorSetLayout;

    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    if (m_device->table().vkAllocateDescriptorSets(m_device->handle(), &allocInfo, &descriptorSet) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate descriptor set");
    }

    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBufferVk->handle();
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(CameraUniformBuffer);

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pBufferInfo = &bufferInfo;

    m_device->table().vkUpdateDescriptorSets(m_device->handle(), 1, &descriptorWrite, 0, nullptr);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = m_device->graphicsQueueFamilyIndex();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (m_enableValidationLayers) std::cout << "[render] creating command pool" << std::endl;
    if (m_device->table().vkCreateCommandPool(m_device->handle(), &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool");
    }

    VkCommandBufferAllocateInfo allocBufferInfo{};
    allocBufferInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocBufferInfo.commandPool = commandPool;
    allocBufferInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocBufferInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    if (m_device->table().vkAllocateCommandBuffers(m_device->handle(), &allocBufferInfo, &commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (m_device->table().vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin command buffer");
    }

    executor.setCommandBuffer(commandBuffer);

    // ---- Frame graph -------------------------------------------------------------------------
    //   scene -> color (transient) -> present (blit into the backbuffer)     [windowed]
    //   scene -> color (transient) -> readback (copy into a staging buffer)  [headless]
    // The graph allocates `color`, and inserts every layout transition and barrier between the passes.
    RenderGraph graph;
    const ResourceHandle color = graph.createImage("color", ImageDesc{width, height, ImageFormat::RGBA8});

    graph.addPass(
        "scene", [&](PassBuilder &b) { b.write(color, ImageUsage::ColorAttachment); },
        [&](PassContext &ctx) {
            auto &gx = static_cast<GraphExecutor &>(ctx);
            const auto &table = m_device->table();
            const VkImageView colorView = gx.view(color);

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = renderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = &colorView;
            framebufferInfo.width = width;
            framebufferInfo.height = height;
            framebufferInfo.layers = 1;
            if (table.vkCreateFramebuffer(m_device->handle(), &framebufferInfo, nullptr, &framebuffer) != VK_SUCCESS) {
                throw std::runtime_error("Failed to create framebuffer");
            }

            VkClearValue clearColor{{0.1f, 0.2f, 0.3f, 1.0f}};
            VkRenderPassBeginInfo renderPassBeginInfo{};
            renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            renderPassBeginInfo.renderPass = renderPass;
            renderPassBeginInfo.framebuffer = framebuffer;
            renderPassBeginInfo.renderArea = {{0, 0}, {width, height}};
            renderPassBeginInfo.clearValueCount = 1;
            renderPassBeginInfo.pClearValues = &clearColor;

            const VkCommandBuffer cmd = gx.commandBuffer();
            table.vkCmdBeginRenderPass(cmd, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
            table.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
            const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
            const VkRect2D scissor{{0, 0}, {width, height}};
            table.vkCmdSetViewport(cmd, 0, 1, &viewport);
            table.vkCmdSetScissor(cmd, 0, 1, &scissor);
            VkBuffer vertexBufferHandle = vertexBufferVk->handle();
            VkDeviceSize offsets[] = {0};
            table.vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBufferHandle, offsets);
            table.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet,
                                          0, nullptr);
            table.vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(albedo),
                                     albedo.data());
            table.vkCmdDraw(cmd, static_cast<uint32_t>(vertices.size()), 1, 0, 0);
            table.vkCmdEndRenderPass(cmd);
        });

    if (windowed) {
        const VkExtent2D extent = m_swapchain->extent();
        const ResourceHandle backbuffer = graph.importImage(
            "backbuffer", ImageDesc{extent.width, extent.height, ImageFormat::RGBA8}, ImageUsage::Undefined,
            ImageUsage::Present);
        executor.bindImported(backbuffer, m_swapchain->images().at(imageIndex));

        graph.addPass(
            "present",
            [&](PassBuilder &b) {
                b.read(color, ImageUsage::TransferSrc);
                b.write(backbuffer, ImageUsage::TransferDst);
            },
            [&](PassContext &ctx) {
                auto &gx = static_cast<GraphExecutor &>(ctx);
                VkImageBlit blit{};
                blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blit.srcOffsets[1] = {static_cast<int32_t>(width), static_cast<int32_t>(height), 1};
                blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                blit.dstOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
                m_device->table().vkCmdBlitImage(gx.commandBuffer(), gx.image(color),
                                                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, gx.image(backbuffer),
                                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST);
            });
    } else {
        VkBufferCreateInfo stagingBufferInfo{};
        stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingBufferInfo.size = static_cast<VkDeviceSize>(width) * height * 4;
        stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo stagingAllocInfo{};
        stagingAllocInfo.usage = VMA_MEMORY_USAGE_CPU_ONLY;
        if (vmaCreateBuffer(m_device->allocator(), &stagingBufferInfo, &stagingAllocInfo, &res.stagingBuffer,
                            &res.stagingAllocation, nullptr) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create staging buffer");
        }

        graph.addPass(
            "readback",
            [&](PassBuilder &b) {
                b.read(color, ImageUsage::TransferSrc);
                b.sideEffects(); // result leaves the graph via the host-visible staging buffer
            },
            [&](PassContext &ctx) {
                auto &gx = static_cast<GraphExecutor &>(ctx);
                VkBufferImageCopy copyRegion{};
                copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
                copyRegion.imageExtent = {width, height, 1};
                m_device->table().vkCmdCopyImageToBuffer(gx.commandBuffer(), gx.image(color),
                                                         VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, res.stagingBuffer, 1,
                                                         &copyRegion);
            });
    }

    graph.execute(executor);

    if (m_device->table().vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to end command buffer");
    }

    // The backbuffer is first used by a transfer (the blit), so that is the stage that waits for it.
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    if (windowed) {
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &res.imageAvailable;
        submitInfo.pWaitDstStageMask = &waitStage;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &res.renderFinished;
    }
    if (m_enableValidationLayers) std::cout << "[render] submitting frame" << std::endl;
    if (m_device->table().vkQueueSubmit(m_device->graphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit command buffer");
    }

    if (windowed) {
        // Out of date or suboptimal: rebuild the swapchain for the next frame.
        if (!m_swapchain->present(imageIndex, res.renderFinished)) {
            recreateSwapchain();
        }
        return;
    }

    m_device->table().vkQueueWaitIdle(m_device->graphicsQueue());

    void *mapped = nullptr;
    vmaMapMemory(m_device->allocator(), res.stagingAllocation, &mapped);
    std::filesystem::create_directories(kOutputPath.parent_path());
    std::ofstream ppm(kOutputPath, std::ios::binary);
    ppm << "P6\n" << width << " " << height << "\n255\n";
    const auto *pixels = static_cast<const unsigned char *>(mapped);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const std::size_t idx = (static_cast<std::size_t>(y) * width + x) * 4;
            ppm.write(reinterpret_cast<const char *>(pixels + idx), 3);
        }
    }
    vmaUnmapMemory(m_device->allocator(), res.stagingAllocation);

    std::cout << "Rendered " << scene.objectCount() << " object(s) and "
              << totalTriangles << " triangle(s)." << std::endl;
    std::cout << "Wrote render output to " << kOutputPath.string() << std::endl;
}

void Renderer::renderFallback(const Scene &scene) {
    constexpr uint32_t width = 640;
    constexpr uint32_t height = 480;
    // Same clear colour as the GPU path (0.1, 0.2, 0.3) so both backends produce the same image.
    std::vector<std::array<uint8_t, 4>> pixels(width * height, {26, 51, 76, 0xff});

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

    writePpm(kOutputPath, width, height, pixels);
    std::cout << "[render] wrote fallback triangle image to " << kOutputPath.string() << "" << std::endl;
}

void Renderer::createInstance() {
    std::cerr << "[instance] creating Vulkan instance" << std::endl;
    std::cerr.flush();
    VkApplicationInfo appInfo = {};
    std::cerr << "[instance] app info initialized" << std::endl;
    std::cerr.flush();
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Phox";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "Phox";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    std::vector<const char *> layers;
    std::cerr << "[instance] setup layers" << std::endl;
    std::cerr.flush();
    if (m_enableValidationLayers) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    std::vector<const char *> extensions;
    std::cerr << "[instance] setup extensions" << std::endl;
    std::cerr.flush();
    if (m_enableValidationLayers) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    // Surface extensions come from the window; headless needs none.
    std::vector<std::string> windowExtensions;
    if (m_window != nullptr) {
        windowExtensions = m_window->requiredVulkanExtensions();
        for (const auto &name : windowExtensions) {
            extensions.push_back(name.c_str());
        }
    }

    VkInstanceCreateInfo createInfo = {};
    std::cerr << "[instance] create info prepared" << std::endl;
    std::cerr.flush();
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();

    VkResult result = vkCreateInstance(&createInfo, nullptr, &m_instance);
    std::cerr << "[instance] vkCreateInstance result=" << result << std::endl;
    std::cerr.flush();
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create instance!");
    }
}

void Renderer::setupDebugMessenger() {
    VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    createInfo.pfnUserCallback = debugUtilsMessengerCallback;

    vkCreateDebugUtilsMessengerEXT(m_instance, &createInfo, nullptr,
                                   &m_debugMessenger);
}

void Renderer::createPipeline() {
    std::cerr << "[pipeline] loading shaders from " << kShaderDir.string() << std::endl;
    m_vertShader = Shader::fromFile(*m_device, (kShaderDir / "basic.vert.spv").string(), ShaderType::VERTEX);
    m_fragShader = Shader::fromFile(*m_device, (kShaderDir / "basic.frag.spv").string(), ShaderType::FRAGMENT);

    std::vector<phox::Shader *> shaders = {m_vertShader.get(), m_fragShader.get()};
    std::unique_ptr<phox::Pipeline> pipeline = m_device->createPipeline(shaders);
    auto *graphics = dynamic_cast<GraphicsPipeline *>(pipeline.get());
    if (graphics == nullptr) {
        throw std::runtime_error("Device::createPipeline did not return a Vulkan graphics pipeline");
    }
    pipeline.release();
    m_pipeline.reset(graphics);
    std::cerr << "[pipeline] graphics pipeline created" << std::endl;
}

void Renderer::createSurface() {
    m_surface = VK_NULL_HANDLE;
    if (m_window == nullptr) {
        std::cerr << "[surface] headless (no window)" << std::endl;
        return;
    }
    std::cerr << "[surface] creating window surface" << std::endl;
    std::uint64_t raw = 0;
    if (!m_window->createVulkanSurface(m_instance, &raw)) {
        throw std::runtime_error("Failed to create window surface");
    }
    std::memcpy(&m_surface, &raw, sizeof(m_surface));
}

void Renderer::createSwapchain() {
    m_swapchain.reset();
    m_windowWidth = m_windowHeight = 0;
    if (m_window == nullptr || m_surface == VK_NULL_HANDLE) {
        return; // headless
    }
    int width = 0, height = 0;
    m_window->framebufferSize(width, height);
    if (width <= 0 || height <= 0) {
        return; // minimised; retried when the window has a size again
    }
    m_swapchain = std::make_unique<Swapchain>(m_device.get(), m_surface, static_cast<uint32_t>(width),
                                              static_cast<uint32_t>(height));
    m_windowWidth = width;
    m_windowHeight = height;
}

void Renderer::recreateSwapchain() {
    m_device->table().vkDeviceWaitIdle(m_device->handle());
    createSwapchain();
}

void Renderer::createLogicalDevice() {
    std::cerr << "[device] selecting physical device" << std::endl;
    const VkPhysicalDevice physicalDevice = pickPhysicalDevice(m_instance, m_surface);

    m_device = std::make_unique<Device>();
    m_device->Initialize(m_instance, physicalDevice, m_surface);
}

Renderer::~Renderer() {
    if (m_initialized) {
        cleanup();
    }
}

}

} // namespace phox
