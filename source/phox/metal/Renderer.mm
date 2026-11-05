#include "phox/metal/Renderer.hpp"

#include "phox/Log.hpp"
#include "phox/RenderGraph.hpp"
#include "phox/RenderView.hpp"
#include "phox/SoftwareRasterizer.hpp"
#include "phox/Window.hpp"
#include "phox/metal/Buffer.hpp"
#include "phox/metal/Conversions.hpp"
#include "phox/metal/Device.hpp"
#include "phox/metal/GraphExecutor.hpp"
#include "phox/metal/MeshCache.hpp"
#include "phox/metal/PipelineCache.hpp"
#include "phox/metal/ShaderInterface.hpp"
#include "phox/metal/ShaderLibrary.hpp"
#include "phox/metal/TransientPool.hpp"
#include "phox/metal/passes/ForwardPass.hpp"
#include "phox/metal/passes/ReadbackPass.hpp"
#include "phox/metal/passes/TonemapPass.hpp"

#import <QuartzCore/CAMetalLayer.h>
#include <dispatch/dispatch.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#ifndef PHOX_SHADER_DIR
#define PHOX_SHADER_DIR "build/shaders"
#endif

namespace {

const std::filesystem::path kShaderDir = std::filesystem::path(PHOX_SHADER_DIR) / "metal";

// The same formats as the Vulkan backend. Depth is always 32-bit float: every Metal GPU can render to it.
constexpr phox::Format kHdrFormat = phox::Format::RGBA16Float;
constexpr phox::Format kHeadlessFormat = phox::Format::RGBA8Unorm;
constexpr phox::Format kDepthFormat = phox::Format::D32Float;
// UNORM like the Vulkan swapchain: colours are treated as display values (see "sRGB output" in the docs).
constexpr MTLPixelFormat kDrawableFormat = MTLPixelFormatBGRA8Unorm;

std::string toLower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

// The system default device, or with @p preferred set, the first whose name contains it (case-insensitive).
id<MTLDevice> pickDevice(const std::string &preferred) {
    if (preferred.empty()) {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        if (device == nil) {
            throw std::runtime_error("No Metal device found");
        }
        return device;
    }
    for (id<MTLDevice> device in MTLCopyAllDevices()) {
        if (toLower(device.name.UTF8String).find(toLower(preferred)) != std::string::npos) {
            return device;
        }
    }
    throw std::runtime_error("No Metal device matching '" + preferred + "'");
}

// With MTL_DEBUG_LAYER set when the device is created, Metal wraps its objects in validating proxies.
bool apiValidationActive(id<MTLDevice> device) {
    return [NSStringFromClass([device class]) rangeOfString:@"Debug"].location != NSNotFound;
}

phox::FrameUniforms frameUniforms(const phox::RenderView &view) {
    phox::FrameUniforms uniforms;
    uniforms.view = view.view;
    uniforms.proj = view.proj;
    uniforms.viewProj = view.viewProj;
    uniforms.cameraPosition = phox::vec4(view.cameraPosition, 1.0f);
    uniforms.lightDirection = phox::vec4(view.lightDirection, view.ambient);
    uniforms.lightColor = phox::vec4(view.lightColor, 1.0f);
    const float width = static_cast<float>(view.extent.width);
    const float height = static_cast<float>(view.extent.height);
    uniforms.resolution = {width, height, 1.0f / width, 1.0f / height};
    return uniforms;
}

} // namespace

namespace phox::mtl {

struct Renderer::State {
    CAMetalLayer *layer = nil; ///< windowed only; owned by the window
    /// Counts free frame slots: taken before a frame is encoded, given back when the GPU completes it.
    dispatch_semaphore_t framesInFlight = nil;
};

Renderer::Renderer() = default;

Renderer::~Renderer() { releaseMetal(); }

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
        @autoreleasepool {
            createDevice();
            m_state = std::make_unique<State>();
            m_state->framesInFlight = dispatch_semaphore_create(kFramesInFlight);
            m_transients = std::make_unique<TransientPool>(*m_device);
            m_meshes = std::make_unique<MeshCache>(*m_device);
            createPipelines();
            if (m_window != nullptr) {
                createLayer();
            }
        }
        log::info("renderer initialized on {} (Metal){}", m_deviceName, m_validation ? " (API validation on)" : "");
    } catch (const std::exception &e) {
        log::warn("Metal initialization failed, using the software fallback renderer: {}", e.what());
        releaseMetal();
        m_useFallback = true;
    }
    m_initialized = true;
}

void Renderer::cleanup() {
    releaseMetal();
    m_initialized = false;
    m_useFallback = false;
}

void Renderer::releaseMetal() {
    if (m_state && m_state->framesInFlight != nil) {
        // Take every slot, so each frame's completion handler has run, then give them back: libdispatch
        // refuses to destroy a semaphore below its initial value.
        for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
            dispatch_semaphore_wait(m_state->framesInFlight, DISPATCH_TIME_FOREVER);
        }
        for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
            dispatch_semaphore_signal(m_state->framesInFlight);
        }
    }
    m_readback.reset();
    m_meshes.reset();
    m_transients.reset();
    m_pipelines.reset();
    m_shaders.reset();
    m_state.reset();
    m_device.reset();
}

RendererInfo Renderer::info() const {
    RendererInfo result;
    if (m_initialized) {
        result.backend = m_useFallback ? RendererBackend::Software : RendererBackend::Metal;
    }
    result.deviceName = m_useFallback ? "software rasterizer" : m_deviceName;
    result.validation = m_validation;
    // Metal's validation layer stops the process on an error rather than reporting it, so the errors
    // counted here are command buffers the GPU failed to execute.
    result.validationErrors = m_gpuErrors.load();
    return result;
}

void Renderer::createDevice() {
    if (m_config.debug) {
        // Metal reads this when the first device is created; a value set outside the process wins.
        setenv("MTL_DEBUG_LAYER", "1", 0);
    }
    id<MTLDevice> device = pickDevice(m_config.preferredDevice);
    m_device = std::make_unique<Device>(device);
    m_deviceName = m_device->name();
    m_validation = apiValidationActive(device);
    if (m_config.debug && !m_validation) {
        log::warn("Metal API validation is not active; set MTL_DEBUG_LAYER=1 before starting the program");
    }
}

void Renderer::createPipelines() {
    m_shaders = std::make_unique<ShaderLibrary>(*m_device, kShaderDir);
    m_pipelines = std::make_unique<PipelineCache>(*m_device, *m_shaders);
    // Build the pipelines now, so missing or broken shaders fail initialization (and select the software
    // fallback) instead of the first frame.
    m_pipelines->get(forwardPipelineDesc(kHdrFormat, kDepthFormat));
    m_pipelines->get(tonemapPipelineDesc(kHeadlessFormat));
}

void Renderer::createLayer() {
    void *layer = m_window->metalLayer();
    if (layer == nullptr) {
        throw std::runtime_error("the window has no CAMetalLayer (create it with GraphicsApi::Metal)");
    }
    m_state->layer = (__bridge CAMetalLayer *)layer;
    m_state->layer.device = m_device->handle();
    m_state->layer.pixelFormat = kDrawableFormat;
    m_state->layer.framebufferOnly = YES; // drawables are only ever render targets
    m_drawableFormat = fromMtl(kDrawableFormat);
    m_pipelines->get(tonemapPipelineDesc(m_drawableFormat));
}

void Renderer::render(const Scene &scene) {
    if (!m_initialized) {
        return;
    }
    if (m_useFallback) {
        renderFallback(scene);
        return;
    }
    @autoreleasepool {
        renderFrame(scene);
    }
}

void Renderer::renderFrame(const Scene &scene) {
    const bool windowed = m_window != nullptr;
    if (windowed) {
        int width = 0, height = 0;
        m_window->framebufferSize(width, height);
        if (width <= 0 || height <= 0) {
            return; // minimised
        }
        const CGSize size = CGSizeMake(width, height);
        if (!CGSizeEqualToSize(m_state->layer.drawableSize, size)) {
            m_state->layer.drawableSize = size; // later drawables have the new size
            log::debug("drawable size {}x{}", width, height);
        }
    }

    // Wait for a free frame slot. It is given back by the frame's completion handler, or below if the
    // frame never gets submitted.
    dispatch_semaphore_t framesInFlight = m_state->framesInFlight;
    dispatch_semaphore_wait(framesInFlight, DISPATCH_TIME_FOREVER);
    bool submitted = false;
    struct SlotGuard {
        dispatch_semaphore_t semaphore;
        const bool &submitted;
        ~SlotGuard() {
            if (!submitted) dispatch_semaphore_signal(semaphore);
        }
    } slotGuard{framesInFlight, submitted};

    m_transients->collect(m_frameNumber);
    m_meshes->collect(m_frameNumber);

    id<CAMetalDrawable> drawable = nil;
    Extent2D extent = m_config.headlessExtent;
    if (windowed) {
        drawable = [m_state->layer nextDrawable];
        if (drawable == nil) {
            log::warn("no drawable became available; skipping the frame");
            return;
        }
        extent = {static_cast<std::uint32_t>(drawable.texture.width),
                  static_cast<std::uint32_t>(drawable.texture.height)};
    }

    const RenderView view = extractView(scene, extent);
    const FrameUniforms uniforms = frameUniforms(view);
    if (!windowed) {
        const std::uint64_t bytes = static_cast<std::uint64_t>(extent.width) * extent.height * 4;
        if (!m_readback || m_readback->size() != bytes) {
            m_readback = m_device->makeBuffer(
                {bytes, usageBit(BufferUsage::TransferDst), MemoryLocation::Readback, "readback"});
        }
    }

    id<MTLCommandBuffer> commandBuffer = nil;
    if (m_validation) {
        // Reports which encoder failed when a command buffer errors.
        MTLCommandBufferDescriptor *descriptor = [MTLCommandBufferDescriptor new];
        descriptor.errorOptions = MTLCommandBufferErrorOptionEncoderExecutionStatus;
        commandBuffer = [m_device->queue() commandBufferWithDescriptor:descriptor];
    } else {
        commandBuffer = [m_device->queue() commandBuffer];
    }
    commandBuffer.label = [NSString stringWithFormat:@"frame %llu", static_cast<unsigned long long>(m_frameNumber)];
    m_meshes->prepare(view, m_frameNumber);

    // ---- Frame graph (the same shape as the Vulkan backend's) -----------------------------------------
    //   forward -> hdr -> tonemap -> drawable                          [windowed]
    //   forward -> hdr -> tonemap -> display -> readback -> host buffer [headless]
    {
        RenderGraph graph(extent);
        GraphExecutor executor(graph, *m_device, *m_transients, m_frameNumber, commandBuffer);
        const ImageHandle hdr = graph.createImage("hdr", ImageDesc{RelativeExtent{1.0f}, kHdrFormat});
        const ImageHandle depth = graph.createImage("depth", ImageDesc{RelativeExtent{1.0f}, kDepthFormat});
        const ForwardPassOutput forward = addForwardPass(graph, view, uniforms, hdr, depth, *m_meshes, *m_pipelines);

        if (windowed) {
            const ImageHandle backbuffer = graph.importImage(
                "drawable", ImageDesc{extent, m_drawableFormat}, ImageUsage::Undefined, ImageUsage::Present);
            executor.bindImage(backbuffer, drawable.texture);
            addTonemapPass(graph, forward.color, backbuffer, view.toneMapping, *m_pipelines);
        } else {
            const ImageHandle display =
                graph.createImage("display", ImageDesc{RelativeExtent{1.0f}, kHeadlessFormat});
            const ImageHandle tonemapped = addTonemapPass(graph, forward.color, display, view.toneMapping, *m_pipelines);
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
    }

    if (windowed) {
        [commandBuffer presentDrawable:drawable];
    }
    std::atomic<std::uint32_t> *gpuErrors = &m_gpuErrors;
    [commandBuffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
        if (completed.status == MTLCommandBufferStatusError) {
            gpuErrors->fetch_add(1);
            log::error("[metal] {} failed: {}", completed.label.UTF8String,
                       completed.error.localizedDescription.UTF8String);
        }
        dispatch_semaphore_signal(framesInFlight); // last: releaseMetal() waits for this
    }];
    [commandBuffer commit];
    submitted = true;
    ++m_frameNumber;

    if (windowed) {
        return;
    }
    // Headless frames are read back straight away, so they do not overlap.
    [commandBuffer waitUntilCompleted];
    if (commandBuffer.status != MTLCommandBufferStatusCompleted) {
        const std::string reason =
            commandBuffer.error != nil ? commandBuffer.error.localizedDescription.UTF8String : "unknown error";
        throw std::runtime_error("the headless frame failed: " + reason);
    }
    const auto *pixels = static_cast<const std::uint8_t *>(m_readback->mapped());
    m_lastFrame.width = extent.width;
    m_lastFrame.height = extent.height;
    m_lastFrame.rgba.assign(pixels, pixels + m_readback->size());
    log::debug("rendered {} draw(s), {} culled", view.draws.size(), view.culled);
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

} // namespace phox::mtl
