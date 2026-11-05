#include "phox/metal/PipelineCache.hpp"

#include "phox/Log.hpp"
#include "phox/metal/Conversions.hpp"
#include "phox/metal/Device.hpp"
#include "phox/metal/ShaderInterface.hpp"
#include "phox/metal/ShaderLibrary.hpp"

#include <stdexcept>
#include <utility>

namespace phox::mtl {

namespace {

void setBlend(MTLRenderPipelineColorAttachmentDescriptor *attachment, BlendMode mode) {
    switch (mode) {
    case BlendMode::Opaque:
        attachment.blendingEnabled = NO;
        return;
    case BlendMode::Alpha:
        attachment.blendingEnabled = YES;
        attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
        attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        return;
    case BlendMode::Additive:
        attachment.blendingEnabled = YES;
        attachment.sourceRGBBlendFactor = MTLBlendFactorOne;
        attachment.destinationRGBBlendFactor = MTLBlendFactorOne;
        attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;
        attachment.destinationAlphaBlendFactor = MTLBlendFactorOne;
        return;
    }
}

} // namespace

// ---- Pipeline ---------------------------------------------------------------------------------------

Pipeline::Pipeline(PipelineDesc desc, id<MTLRenderPipelineState> state, id<MTLDepthStencilState> depthStencil)
    : phox::Pipeline(std::move(desc)), m_state(state), m_depthStencil(depthStencil) {}

Pipeline::~Pipeline() = default;

MTLPrimitiveType Pipeline::primitive() const { return toMtl(m_desc.topology); }

void Pipeline::bind(id<MTLRenderCommandEncoder> encoder) const {
    [encoder setRenderPipelineState:m_state];
    [encoder setDepthStencilState:m_depthStencil];
    [encoder setCullMode:toMtl(m_desc.raster.cull)];
    [encoder setFrontFacingWinding:toMtl(m_desc.raster.frontFace)];
}

// ---- PipelineCache ----------------------------------------------------------------------------------

PipelineCache::PipelineCache(Device &device, ShaderLibrary &shaders) : m_device(device), m_shaders(shaders) {}

PipelineCache::~PipelineCache() = default;

const Pipeline &PipelineCache::get(const PipelineDesc &desc) {
    if (auto it = m_pipelines.find(desc); it != m_pipelines.end()) {
        return *it->second;
    }
    auto pipeline = create(desc);
    log::debug("pipeline cache: created '{}' ({} cached)", desc.name, m_pipelines.size() + 1);
    return *m_pipelines.emplace(desc, std::move(pipeline)).first->second;
}

std::unique_ptr<Pipeline> PipelineCache::create(const PipelineDesc &desc) {
    if (desc.vertexShader.empty()) {
        throw std::logic_error("pipeline '" + desc.name + "' has no vertex shader");
    }
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.label = [NSString stringWithUTF8String:desc.name.c_str()];
    descriptor.vertexFunction = m_shaders.function(desc.vertexShader);
    if (!desc.fragmentShader.empty()) {
        descriptor.fragmentFunction = m_shaders.function(desc.fragmentShader);
    }

    // Attribute i is [[attribute(i)]] in MSL, as it is location i in GLSL.
    const auto &attributes = desc.vertexLayout.attributes();
    if (!attributes.empty()) {
        MTLVertexDescriptor *vertex = [MTLVertexDescriptor vertexDescriptor];
        for (std::size_t i = 0; i < attributes.size(); ++i) {
            vertex.attributes[i].format = toMtl(attributes[i].format);
            vertex.attributes[i].offset = attributes[i].offset;
            vertex.attributes[i].bufferIndex = kVertexBufferIndex;
        }
        vertex.layouts[kVertexBufferIndex].stride = desc.vertexLayout.stride();
        vertex.layouts[kVertexBufferIndex].stepFunction = MTLVertexStepFunctionPerVertex;
        descriptor.vertexDescriptor = vertex;
    }

    for (std::size_t i = 0; i < desc.colorFormats.size(); ++i) {
        descriptor.colorAttachments[i].pixelFormat = toMtl(desc.colorFormats[i]);
        setBlend(descriptor.colorAttachments[i], i < desc.blend.size() ? desc.blend[i] : BlendMode::Opaque);
    }
    if (isDepthFormat(desc.depthFormat)) {
        descriptor.depthAttachmentPixelFormat = toMtl(desc.depthFormat);
    }
    if (hasStencil(desc.depthFormat)) {
        descriptor.stencilAttachmentPixelFormat = toMtl(desc.depthFormat);
    }
    descriptor.inputPrimitiveTopology = topologyClass(desc.topology);

    NSError *error = nil;
    id<MTLRenderPipelineState> state = [m_device.handle() newRenderPipelineStateWithDescriptor:descriptor error:&error];
    if (state == nil) {
        const std::string message = error != nil ? error.localizedDescription.UTF8String : "unknown error";
        throw std::runtime_error("failed to create pipeline '" + desc.name + "': " + message);
    }

    MTLDepthStencilDescriptor *depth = [MTLDepthStencilDescriptor new];
    depth.depthCompareFunction = desc.depth.test ? toMtl(desc.depth.compare) : MTLCompareFunctionAlways;
    depth.depthWriteEnabled = desc.depth.test && desc.depth.write; // as in Vulkan: no test, no writes
    id<MTLDepthStencilState> depthStencil = [m_device.handle() newDepthStencilStateWithDescriptor:depth];
    if (depthStencil == nil) {
        throw std::runtime_error("failed to create the depth-stencil state of '" + desc.name + "'");
    }
    return std::make_unique<Pipeline>(desc, state, depthStencil);
}

} // namespace phox::mtl
