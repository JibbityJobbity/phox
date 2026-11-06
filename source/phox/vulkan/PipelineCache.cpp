#include "phox/vulkan/PipelineCache.hpp"

#include "phox/Log.hpp"
#include "phox/vulkan/Conversions.hpp"
#include "phox/vulkan/Device.hpp"
#include "phox/vulkan/ShaderLibrary.hpp"
#include "phox/vulkan/pipeline/Shader.hpp"

#include <stdexcept>

namespace phox::vk {

namespace {

VkShaderStageFlags toVkStages(ShaderStages stages) {
    VkShaderStageFlags flags = 0;
    if (stages & kVertexStage) flags |= VK_SHADER_STAGE_VERTEX_BIT;
    if (stages & kFragmentStage) flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
    if (stages & kComputeStage) flags |= VK_SHADER_STAGE_COMPUTE_BIT;
    if (stages & kTaskStage) flags |= VK_SHADER_STAGE_TASK_BIT_EXT;
    if (stages & kMeshStage) flags |= VK_SHADER_STAGE_MESH_BIT_EXT;
    return flags;
}

VkPrimitiveTopology toVk(Topology topology) {
    switch (topology) {
    case Topology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case Topology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case Topology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case Topology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags toVk(CullMode mode) {
    switch (mode) {
    case CullMode::None: return VK_CULL_MODE_NONE;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    }
    return VK_CULL_MODE_NONE;
}

VkCompareOp toVk(CompareOp op) {
    switch (op) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessOrEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterOrEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS;
}

VkPipelineColorBlendAttachmentState toVk(BlendMode mode) {
    VkPipelineColorBlendAttachmentState state{};
    state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
    switch (mode) {
    case BlendMode::Opaque:
        break;
    case BlendMode::Alpha:
        state.blendEnable = VK_TRUE;
        state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.colorBlendOp = VK_BLEND_OP_ADD;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        state.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    case BlendMode::Additive:
        state.blendEnable = VK_TRUE;
        state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        state.colorBlendOp = VK_BLEND_OP_ADD;
        state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        state.alphaBlendOp = VK_BLEND_OP_ADD;
        break;
    }
    return state;
}

} // namespace

Pipeline::Pipeline(Device &device, PipelineDesc desc, VkPipeline pipeline, VkPipelineLayout layout)
    : phox::Pipeline(std::move(desc)), m_device(device), m_pipeline(pipeline), m_layout(layout) {}

Pipeline::~Pipeline() { m_device.table().vkDestroyPipeline(m_device.handle(), m_pipeline, nullptr); }

PipelineCache::PipelineCache(Device &device, ShaderLibrary &shaders) : m_device(device), m_shaders(shaders) {
    const VkDevice dev = device.handle();
    const auto &table = device.table();

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    if (table.vkCreateSampler(dev, &samplerInfo, nullptr, &m_linearClamp) != VK_SUCCESS) {
        throw std::runtime_error("failed to create the linear clamp sampler");
    }

    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    if (table.vkCreateSampler(dev, &samplerInfo, nullptr, &m_nearestClamp) != VK_SUCCESS) {
        table.vkDestroySampler(dev, m_linearClamp, nullptr);
        throw std::runtime_error("failed to create the nearest clamp sampler");
    }

    const auto binding = [](std::uint32_t index, VkDescriptorType type, VkShaderStageFlags stages,
                            const VkSampler *sampler = nullptr) {
        VkDescriptorSetLayoutBinding result{};
        result.binding = index;
        result.descriptorType = type;
        result.descriptorCount = 1;
        result.stageFlags = stages;
        result.pImmutableSamplers = sampler;
        return result;
    };
    VkShaderStageFlags frameStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    if (device.meshShaders()) {
        frameStages |= VK_SHADER_STAGE_TASK_BIT_EXT | VK_SHADER_STAGE_MESH_BIT_EXT;
    }
    // Samplers are baked into the layouts, so a descriptor write only needs a view. Empty: the layout
    // needs mesh shaders and the device has none.
    std::array<std::vector<VkDescriptorSetLayoutBinding>, kSetLayoutCount> layouts;
    layouts[static_cast<std::size_t>(SetLayout::Frame)] = {
        binding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, frameStages),
        binding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT)};
    layouts[static_cast<std::size_t>(SetLayout::Texture)] = {
        binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, &m_linearClamp)};
    if (device.meshShaders()) {
        layouts[static_cast<std::size_t>(SetLayout::MeshletVisibility)] = {
            binding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_TASK_BIT_EXT)};
        layouts[static_cast<std::size_t>(SetLayout::DepthPyramid)] = {
            binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_TASK_BIT_EXT, &m_nearestClamp)};
    }
    layouts[static_cast<std::size_t>(SetLayout::DepthPyramidBuild)] = {
        binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_COMPUTE_BIT, &m_nearestClamp),
        binding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT),
        binding(2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_COMPUTE_BIT)};
    // Each texture brings its own sampler (glTF samplers differ per texture), so no immutable sampler.
    VkDescriptorSetLayoutBinding textures = binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT);
    textures.descriptorCount = device.bindlessTextureCount();
    layouts[static_cast<std::size_t>(SetLayout::Bindless)] = {textures};
    for (std::uint32_t i = 0; i < 4; ++i) {
        layouts[static_cast<std::size_t>(SetLayout::ComputeStorage)].push_back(
            binding(i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_SHADER_STAGE_COMPUTE_BIT));
    }

    for (std::size_t i = 0; i < m_setLayouts.size(); ++i) {
        if (layouts[i].empty()) {
            continue;
        }
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<std::uint32_t>(layouts[i].size());
        info.pBindings = layouts[i].data();
        const VkDescriptorBindingFlags bindlessFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
                                                       VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT |
                                                       VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
        VkDescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
        flagsInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        flagsInfo.bindingCount = 1;
        flagsInfo.pBindingFlags = &bindlessFlags;
        if (static_cast<SetLayout>(i) == SetLayout::Bindless) {
            info.pNext = &flagsInfo;
            info.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        }
        if (table.vkCreateDescriptorSetLayout(dev, &info, nullptr, &m_setLayouts[i]) != VK_SUCCESS) {
            for (VkDescriptorSetLayout layout : m_setLayouts) table.vkDestroyDescriptorSetLayout(dev, layout, nullptr);
            table.vkDestroySampler(dev, m_nearestClamp, nullptr);
            table.vkDestroySampler(dev, m_linearClamp, nullptr);
            throw std::runtime_error("failed to create a descriptor set layout");
        }
    }
}

PipelineCache::~PipelineCache() {
    m_pipelines.clear();
    const VkDevice dev = m_device.handle();
    const auto &table = m_device.table();
    for (const LayoutEntry &entry : m_layouts) table.vkDestroyPipelineLayout(dev, entry.layout, nullptr);
    for (VkDescriptorSetLayout layout : m_setLayouts) table.vkDestroyDescriptorSetLayout(dev, layout, nullptr);
    table.vkDestroySampler(dev, m_nearestClamp, nullptr);
    table.vkDestroySampler(dev, m_linearClamp, nullptr);
}

VkDescriptorSetLayout PipelineCache::setLayout(SetLayout layout) const {
    const VkDescriptorSetLayout result = m_setLayouts.at(static_cast<std::size_t>(layout));
    if (result == VK_NULL_HANDLE) {
        throw std::runtime_error("this set layout needs mesh shaders, which the device does not have");
    }
    return result;
}

const Pipeline &PipelineCache::get(const PipelineDesc &desc) {
    if (auto it = m_pipelines.find(desc); it != m_pipelines.end()) {
        return *it->second;
    }
    auto pipeline = create(desc);
    log::debug("pipeline cache: created '{}' ({} cached)", desc.name, m_pipelines.size() + 1);
    return *m_pipelines.emplace(desc, std::move(pipeline)).first->second;
}

VkPipelineLayout PipelineCache::pipelineLayout(const PipelineDesc &desc) {
    for (const LayoutEntry &entry : m_layouts) {
        if (entry.sets == desc.setLayouts && entry.pushConstants == desc.pushConstants) {
            return entry.layout;
        }
    }
    std::vector<VkDescriptorSetLayout> sets;
    for (SetLayout layout : desc.setLayouts) sets.push_back(setLayout(layout));
    std::vector<VkPushConstantRange> ranges;
    for (const PushConstantRange &range : desc.pushConstants) {
        ranges.push_back({toVkStages(range.stages), range.offset, range.size});
    }
    VkPipelineLayoutCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.setLayoutCount = static_cast<std::uint32_t>(sets.size());
    info.pSetLayouts = sets.data();
    info.pushConstantRangeCount = static_cast<std::uint32_t>(ranges.size());
    info.pPushConstantRanges = ranges.data();
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (m_device.table().vkCreatePipelineLayout(m_device.handle(), &info, nullptr, &layout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create a pipeline layout for '" + desc.name + "'");
    }
    m_layouts.push_back({desc.setLayouts, desc.pushConstants, layout});
    return layout;
}

std::unique_ptr<Pipeline> PipelineCache::createCompute(const PipelineDesc &desc) {
    if (!desc.vertexShader.empty() || !desc.meshShader.empty() || !desc.fragmentShader.empty()) {
        throw std::logic_error("compute pipeline '" + desc.name + "' has graphics shaders");
    }
    const VkPipelineLayout layout = pipelineLayout(desc);
    VkComputePipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    info.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = m_shaders.get(desc.computeShader).module();
    info.stage.pName = "main";
    info.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (m_device.table().vkCreateComputePipelines(m_device.handle(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline '" + desc.name + "'");
    }
    m_device.setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(pipeline), desc.name);
    return std::make_unique<Pipeline>(m_device, desc, pipeline, layout);
}

std::unique_ptr<Pipeline> PipelineCache::create(const PipelineDesc &desc) {
    if (!desc.computeShader.empty()) {
        return createCompute(desc);
    }
    const bool mesh = !desc.meshShader.empty();
    if (desc.vertexShader.empty() == !mesh) {
        throw std::logic_error("pipeline '" + desc.name + "' needs either a vertex shader or a mesh shader");
    }
    if (!desc.taskShader.empty() && !mesh) {
        throw std::logic_error("pipeline '" + desc.name + "' has a task shader but no mesh shader");
    }
    if (mesh && !m_device.meshShaders()) {
        throw std::runtime_error("pipeline '" + desc.name + "' needs mesh shaders, which the device does not have");
    }
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    const auto addStage = [&](const std::string &name, VkShaderStageFlagBits stage) {
        VkPipelineShaderStageCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        info.stage = stage;
        info.module = m_shaders.get(name).module();
        info.pName = "main";
        stages.push_back(info);
    };
    if (mesh) {
        if (!desc.taskShader.empty()) {
            addStage(desc.taskShader, VK_SHADER_STAGE_TASK_BIT_EXT);
        }
        addStage(desc.meshShader, VK_SHADER_STAGE_MESH_BIT_EXT);
    } else {
        addStage(desc.vertexShader, VK_SHADER_STAGE_VERTEX_BIT);
    }
    if (!desc.fragmentShader.empty()) {
        addStage(desc.fragmentShader, VK_SHADER_STAGE_FRAGMENT_BIT);
    }

    VkVertexInputBindingDescription binding{0, desc.vertexLayout.stride(), VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const VertexAttribute &attribute : desc.vertexLayout.attributes()) {
        attributes.push_back({static_cast<std::uint32_t>(attributes.size()), 0, toVkFormat(attribute.format),
                              attribute.offset});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (!attributes.empty()) {
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = toVk(desc.topology);

    // Viewport and scissor are dynamic; the graph executor sets them to the render area.
    VkPipelineViewportStateCreateInfo viewport{};
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{};
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo raster{};
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.lineWidth = 1.0f;
    raster.cullMode = toVk(desc.raster.cull);
    raster.frontFace = desc.raster.frontFace == FrontFace::CounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE
                                                                            : VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineDepthStencilStateCreateInfo depth{};
    depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth.depthTestEnable = desc.depth.test ? VK_TRUE : VK_FALSE;
    depth.depthWriteEnable = desc.depth.write ? VK_TRUE : VK_FALSE;
    depth.depthCompareOp = toVk(desc.depth.compare);

    std::vector<VkPipelineColorBlendAttachmentState> blends;
    for (std::size_t i = 0; i < desc.colorFormats.size(); ++i) {
        blends.push_back(toVk(i < desc.blend.size() ? desc.blend[i] : BlendMode::Opaque));
    }
    VkPipelineColorBlendStateCreateInfo blend{};
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = static_cast<std::uint32_t>(blends.size());
    blend.pAttachments = blends.data();

    // Dynamic rendering: the pipeline names its attachment formats instead of a render pass.
    std::vector<VkFormat> colorFormats;
    for (Format format : desc.colorFormats) colorFormats.push_back(toVkFormat(format));
    VkPipelineRenderingCreateInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering.colorAttachmentCount = static_cast<std::uint32_t>(colorFormats.size());
    rendering.pColorAttachmentFormats = colorFormats.data();
    rendering.depthAttachmentFormat = isDepthFormat(desc.depthFormat) ? toVkFormat(desc.depthFormat) : VK_FORMAT_UNDEFINED;
    rendering.stencilAttachmentFormat = hasStencil(desc.depthFormat) ? toVkFormat(desc.depthFormat) : VK_FORMAT_UNDEFINED;

    const VkPipelineLayout layout = pipelineLayout(desc);
    VkGraphicsPipelineCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext = &rendering;
    info.stageCount = static_cast<std::uint32_t>(stages.size());
    info.pStages = stages.data();
    // Mesh shaders produce primitives themselves: no vertex input or input assembly.
    info.pVertexInputState = mesh ? nullptr : &vertexInput;
    info.pInputAssemblyState = mesh ? nullptr : &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depth;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = layout;
    VkPipeline pipeline = VK_NULL_HANDLE;
    if (m_device.table().vkCreateGraphicsPipelines(m_device.handle(), VK_NULL_HANDLE, 1, &info, nullptr, &pipeline) !=
        VK_SUCCESS) {
        throw std::runtime_error("failed to create pipeline '" + desc.name + "'");
    }
    m_device.setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<std::uint64_t>(pipeline), desc.name);
    return std::make_unique<Pipeline>(m_device, desc, pipeline, layout);
}

} // namespace phox::vk
