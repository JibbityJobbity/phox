#include "VulkanDevice.hpp"

#include <algorithm>
#include <map>

namespace phox::vulkan {

namespace {

VkDescriptorType toVk(rhi::BindingType type) {
    switch (type) {
    case rhi::BindingType::UniformBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    case rhi::BindingType::StorageBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    case rhi::BindingType::SampledTexture: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    case rhi::BindingType::StorageTexture: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    }
    return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

VkFormat toVk(rhi::VertexFormat format) {
    switch (format) {
    case rhi::VertexFormat::Float32: return VK_FORMAT_R32_SFLOAT;
    case rhi::VertexFormat::Float32x2: return VK_FORMAT_R32G32_SFLOAT;
    case rhi::VertexFormat::Float32x3: return VK_FORMAT_R32G32B32_SFLOAT;
    case rhi::VertexFormat::Float32x4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case rhi::VertexFormat::UInt32: return VK_FORMAT_R32_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkPrimitiveTopology toVk(rhi::Topology topology) {
    switch (topology) {
    case rhi::Topology::TriangleList: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case rhi::Topology::LineList: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case rhi::Topology::PointList: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    }
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
}

VkCullModeFlags toVk(rhi::CullMode mode) {
    switch (mode) {
    case rhi::CullMode::None: return VK_CULL_MODE_NONE;
    case rhi::CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    case rhi::CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    }
    return VK_CULL_MODE_NONE;
}

} // namespace

PipelineData VulkanDevice::buildPipelineLayout(const std::vector<rhi::Binding> &bindings,
                                               const std::vector<rhi::PushConstantRange> &pushConstants,
                                               bool compute) {
    PipelineData data;
    data.bindPoint = compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;

    // The same (set, binding) may be declared once per stage; merge the stage flags.
    struct Merged {
        rhi::BindingType type;
        VkShaderStageFlags stages;
    };
    std::map<std::pair<uint32_t, uint32_t>, Merged> merged;
    uint32_t maxSet = 0;
    for (const rhi::Binding &b : bindings) {
        const auto [it, inserted] = merged.try_emplace({b.set, b.binding}, Merged{b.type, static_cast<VkShaderStageFlags>(toVk(b.stage))});
        if (!inserted) {
            if (it->second.type != b.type) {
                throw std::invalid_argument("pipeline: the same set/binding is declared with different types");
            }
            it->second.stages |= toVk(b.stage);
        }
        maxSet = std::max(maxSet, b.set);
    }
    for (const rhi::PushConstantRange &range : pushConstants) {
        if (range.size == 0 || range.offset % 4 != 0 || range.size % 4 != 0 ||
            range.offset + range.size > m_capabilities.maxPushConstantBytes) {
            throw std::invalid_argument("pipeline: invalid push constant range");
        }
    }

    const uint32_t setCount = merged.empty() ? 0 : maxSet + 1;
    data.pushDescriptors = m_pushDescriptors && setCount == 1;

    try {
        for (uint32_t set = 0; set < setCount; ++set) {
            std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
            for (const auto &[key, m] : merged) {
                if (key.first != set) {
                    continue;
                }
                VkDescriptorSetLayoutBinding binding{};
                binding.binding = key.second;
                binding.descriptorType = toVk(m.type);
                binding.descriptorCount = 1;
                binding.stageFlags = m.stages;
                if (m.type == rhi::BindingType::SampledTexture) {
                    binding.pImmutableSamplers = &m_defaultSampler;
                }
                layoutBindings.push_back(binding);
                data.bindings.push_back({set, key.second, m.type});
            }
            VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
            info.flags = data.pushDescriptors ? VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR : 0;
            info.bindingCount = static_cast<uint32_t>(layoutBindings.size());
            info.pBindings = layoutBindings.data();
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            PHOX_VK_CHECK(m_table.vkCreateDescriptorSetLayout(m_device, &info, nullptr, &layout));
            data.setLayouts.push_back(layout);
        }

        std::vector<VkPushConstantRange> ranges;
        for (const rhi::PushConstantRange &range : pushConstants) {
            ranges.push_back({static_cast<VkShaderStageFlags>(toVk(range.stage)), range.offset, range.size});
        }
        VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        layoutInfo.setLayoutCount = static_cast<uint32_t>(data.setLayouts.size());
        layoutInfo.pSetLayouts = data.setLayouts.data();
        layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(ranges.size());
        layoutInfo.pPushConstantRanges = ranges.data();
        PHOX_VK_CHECK(m_table.vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &data.layout));
    } catch (...) {
        destroyPipelineData(data);
        throw;
    }
    return data;
}

rhi::PipelineHandle VulkanDevice::createGraphicsPipeline(const rhi::GraphicsPipelineDesc &desc) {
    // ---- shaders ----
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    std::vector<rhi::ShaderStage> seen;
    bool hasVertex = false, hasMesh = false, hasFragment = false, hasTask = false;
    for (const rhi::ShaderHandle handle : desc.shaders) {
        const ShaderData &shader = m_shaders.get(handle);
        if (std::find(seen.begin(), seen.end(), shader.stage) != seen.end()) {
            throw std::invalid_argument("createGraphicsPipeline: duplicate shader stage");
        }
        seen.push_back(shader.stage);
        hasVertex |= shader.stage == rhi::ShaderStage::Vertex;
        hasMesh |= shader.stage == rhi::ShaderStage::Mesh;
        hasTask |= shader.stage == rhi::ShaderStage::Task;
        hasFragment |= shader.stage == rhi::ShaderStage::Fragment;
        if (shader.stage == rhi::ShaderStage::Compute) {
            throw std::invalid_argument("createGraphicsPipeline: compute shader in a graphics pipeline");
        }
        VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
        stage.stage = toVk(shader.stage);
        stage.module = shader.module;
        stage.pName = shader.entry.c_str();
        stages.push_back(stage);
    }
    if (!hasFragment || (!hasVertex && !hasMesh) || (hasVertex && (hasMesh || hasTask)) || (hasTask && !hasMesh)) {
        throw std::invalid_argument(
            "createGraphicsPipeline: needs Vertex + Fragment, or [Task +] Mesh + Fragment shaders");
    }
    if (hasMesh && !m_meshShaders) {
        throw std::invalid_argument("createGraphicsPipeline: mesh pipelines require Feature::MeshShader");
    }

    PipelineData data = buildPipelineLayout(desc.bindings, desc.pushConstants, false);

    // ---- fixed function ----
    VkVertexInputBindingDescription vertexBinding{0, desc.vertexLayout.stride, VK_VERTEX_INPUT_RATE_VERTEX};
    std::vector<VkVertexInputAttributeDescription> vertexAttributes;
    for (const rhi::VertexAttribute &a : desc.vertexLayout.attributes) {
        vertexAttributes.push_back({a.location, 0, toVk(a.format), a.offset});
    }
    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = vertexAttributes.empty() ? 0 : 1;
    vertexInput.pVertexBindingDescriptions = &vertexBinding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = toVk(desc.topology);

    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;

    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = toVk(desc.cull);
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;

    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Depth test and write (compare LESS) are on exactly when the pipeline has a depth format.
    VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    const bool hasDepth = desc.depthFormat != rhi::Format::Undefined;
    depthStencil.depthTestEnable = hasDepth ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = hasDepth ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(desc.colorFormats.size());
    for (VkPipelineColorBlendAttachmentState &a : blendAttachments) {
        a.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                           VK_COLOR_COMPONENT_A_BIT;
    }
    VkPipelineColorBlendStateCreateInfo colorBlend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlend.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    colorBlend.pAttachments = blendAttachments.data();

    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;

    std::vector<VkFormat> colorFormats;
    for (const rhi::Format f : desc.colorFormats) {
        colorFormats.push_back(toVk(f));
    }
    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = static_cast<uint32_t>(colorFormats.size());
    rendering.pColorAttachmentFormats = colorFormats.data();
    rendering.depthAttachmentFormat = toVk(desc.depthFormat);
    rendering.stencilAttachmentFormat =
        (aspectOf(desc.depthFormat) & VK_IMAGE_ASPECT_STENCIL_BIT) ? toVk(desc.depthFormat) : VK_FORMAT_UNDEFINED;

    VkGraphicsPipelineCreateInfo info{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = static_cast<uint32_t>(stages.size());
    info.pStages = stages.data();
    info.pVertexInputState = hasMesh ? nullptr : &vertexInput;
    info.pInputAssemblyState = hasMesh ? nullptr : &inputAssembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pDepthStencilState = &depthStencil;
    info.pColorBlendState = &colorBlend;
    info.pDynamicState = &dynamic;
    info.layout = data.layout;

    const VkResult result = m_table.vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &data.pipeline);
    if (result != VK_SUCCESS) {
        destroyPipelineData(data);
        throwVk("vkCreateGraphicsPipelines", result);
    }
    setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(data.pipeline), desc.debugName);
    return m_pipelines.create(std::move(data));
}

rhi::PipelineHandle VulkanDevice::createComputePipeline(const rhi::ComputePipelineDesc &desc) {
    const ShaderData &shader = m_shaders.get(desc.shader);
    if (shader.stage != rhi::ShaderStage::Compute) {
        throw std::invalid_argument("createComputePipeline: shader is not a Compute shader");
    }
    PipelineData data = buildPipelineLayout(desc.bindings, desc.pushConstants, true);

    VkComputePipelineCreateInfo info{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    info.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    info.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    info.stage.module = shader.module;
    info.stage.pName = shader.entry.c_str();
    info.layout = data.layout;

    const VkResult result = m_table.vkCreateComputePipelines(m_device, VK_NULL_HANDLE, 1, &info, nullptr, &data.pipeline);
    if (result != VK_SUCCESS) {
        destroyPipelineData(data);
        throwVk("vkCreateComputePipelines", result);
    }
    setObjectName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(data.pipeline), desc.debugName);
    return m_pipelines.create(std::move(data));
}

} // namespace phox::vulkan
