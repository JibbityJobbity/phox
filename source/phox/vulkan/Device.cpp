#define VMA_IMPLEMENTATION

#include "phox/vulkan/Device.hpp"

#include "phox/Log.hpp"
#include "phox/VertexLayout.hpp"
#include "phox/vulkan/Buffer.hpp"
#include "phox/vulkan/Conversions.hpp"
#include "phox/vulkan/Image.hpp"
#include "phox/vulkan/pipeline/GraphicsPipeline.hpp"
#include "phox/vulkan/pipeline/Shader.hpp"

#include <stdexcept>
#include <vector>

namespace phox::vk {

namespace {

struct QueueFamilies {
    std::uint32_t graphics = UINT32_MAX;
    std::uint32_t present = UINT32_MAX;
};

// Prefers one family that can both draw and present, so images never change queue ownership.
QueueFamilies pickQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &count, families.data());

    QueueFamilies result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const bool graphics = (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        VkBool32 present = VK_FALSE;
        if (surface != VK_NULL_HANDLE) {
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &present);
        }
        if (graphics && (present == VK_TRUE || surface == VK_NULL_HANDLE)) {
            return {i, i};
        }
        if (graphics && result.graphics == UINT32_MAX) result.graphics = i;
        if (present == VK_TRUE && result.present == UINT32_MAX) result.present = i;
    }
    if (result.graphics == UINT32_MAX) {
        throw std::runtime_error("the device has no graphics queue");
    }
    if (surface != VK_NULL_HANDLE && result.present == UINT32_MAX) {
        throw std::runtime_error("the device cannot present to the window surface");
    }
    return result;
}

VmaAllocationCreateInfo allocationInfoFor(MemoryLocation memory) {
    VmaAllocationCreateInfo info{};
    switch (memory) {
    case MemoryLocation::DeviceLocal:
        info.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
        break;
    case MemoryLocation::Upload:
        info.usage = VMA_MEMORY_USAGE_AUTO;
        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    case MemoryLocation::Readback:
        info.usage = VMA_MEMORY_USAGE_AUTO;
        info.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        break;
    }
    return info;
}

} // namespace

Device::Device(VkInstance instance, VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, bool objectNames)
    : m_instance(instance), m_physicalDevice(physicalDevice), m_objectNames(objectNames) {
    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_properties);
    const QueueFamilies families = pickQueueFamilies(m_physicalDevice, surface);
    m_graphicsFamily = families.graphics;
    m_presentFamily = surface != VK_NULL_HANDLE ? families.present : families.graphics;

    const float priority = 1.0f;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (std::uint32_t family : {m_graphicsFamily, m_presentFamily}) {
        if (!queueInfos.empty() && queueInfos.front().queueFamilyIndex == family) {
            continue;
        }
        VkDeviceQueueCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        info.queueFamilyIndex = family;
        info.queueCount = 1;
        info.pQueuePriorities = &priority;
        queueInfos.push_back(info);
    }

    std::vector<const char *> extensions;
    if (surface != VK_NULL_HANDLE) {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    createInfo.pQueueCreateInfos = queueInfos.data();
    createInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    if (vkCreateDevice(m_physicalDevice, &createInfo, nullptr, &m_device) != VK_SUCCESS) {
        throw std::runtime_error("failed to create the logical device");
    }
    volkLoadDeviceTable(&m_table, m_device);
    m_table.vkGetDeviceQueue(m_device, m_graphicsFamily, 0, &m_graphicsQueue);
    m_table.vkGetDeviceQueue(m_device, m_presentFamily, 0, &m_presentQueue);

    // VMA is built without static or dynamic function loading, so it gets every entry point from here.
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    functions.vkGetPhysicalDeviceProperties = vkGetPhysicalDeviceProperties;
    functions.vkGetPhysicalDeviceMemoryProperties = vkGetPhysicalDeviceMemoryProperties;
    functions.vkAllocateMemory = m_table.vkAllocateMemory;
    functions.vkFreeMemory = m_table.vkFreeMemory;
    functions.vkMapMemory = m_table.vkMapMemory;
    functions.vkUnmapMemory = m_table.vkUnmapMemory;
    functions.vkFlushMappedMemoryRanges = m_table.vkFlushMappedMemoryRanges;
    functions.vkInvalidateMappedMemoryRanges = m_table.vkInvalidateMappedMemoryRanges;
    functions.vkBindBufferMemory = m_table.vkBindBufferMemory;
    functions.vkBindImageMemory = m_table.vkBindImageMemory;
    functions.vkGetBufferMemoryRequirements = m_table.vkGetBufferMemoryRequirements;
    functions.vkGetImageMemoryRequirements = m_table.vkGetImageMemoryRequirements;
    functions.vkCreateBuffer = m_table.vkCreateBuffer;
    functions.vkDestroyBuffer = m_table.vkDestroyBuffer;
    functions.vkCreateImage = m_table.vkCreateImage;
    functions.vkDestroyImage = m_table.vkDestroyImage;
    functions.vkCmdCopyBuffer = m_table.vkCmdCopyBuffer;
    // Vulkan 1.1 core entry points; VMA reads them through its *2KHR slots.
    functions.vkGetBufferMemoryRequirements2KHR = m_table.vkGetBufferMemoryRequirements2;
    functions.vkGetImageMemoryRequirements2KHR = m_table.vkGetImageMemoryRequirements2;
    functions.vkBindBufferMemory2KHR = m_table.vkBindBufferMemory2;
    functions.vkBindImageMemory2KHR = m_table.vkBindImageMemory2;
    functions.vkGetPhysicalDeviceMemoryProperties2KHR = vkGetPhysicalDeviceMemoryProperties2;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = m_physicalDevice;
    allocatorInfo.device = m_device;
    allocatorInfo.instance = m_instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2; // must match the instance
    allocatorInfo.pVulkanFunctions = &functions;
    if (vmaCreateAllocator(&allocatorInfo, &m_allocator) != VK_SUCCESS) {
        m_table.vkDestroyDevice(m_device, nullptr);
        throw std::runtime_error("failed to create the memory allocator");
    }
}

Device::~Device() {
    m_table.vkDeviceWaitIdle(m_device);
    vmaDestroyAllocator(m_allocator);
    m_table.vkDestroyDevice(m_device, nullptr);
}

void Device::waitIdle() { m_table.vkDeviceWaitIdle(m_device); }

void Device::setObjectName(VkObjectType type, std::uint64_t handle, const std::string &name) const {
    if (!m_objectNames || name.empty() || vkSetDebugUtilsObjectNameEXT == nullptr) {
        return;
    }
    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name.c_str();
    vkSetDebugUtilsObjectNameEXT(m_device, &info);
}

std::unique_ptr<phox::Buffer> Device::createBuffer(const BufferCreateInfo &info) { return makeBuffer(info); }

std::unique_ptr<phox::Image> Device::createImage(const ImageCreateInfo &info) { return makeImage(info); }

std::unique_ptr<Buffer> Device::makeBuffer(const BufferCreateInfo &info) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = info.size;
    bufferInfo.usage = toVkBufferUsage(info.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE; // buffers are only used on the graphics queue
    if (bufferInfo.usage == 0) {
        throw std::logic_error("buffer '" + info.name + "' has no GPU usage");
    }

    const VmaAllocationCreateInfo allocationInfo = allocationInfoFor(info.memory);
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    VmaAllocationInfo allocated{};
    if (vmaCreateBuffer(m_allocator, &bufferInfo, &allocationInfo, &buffer, &allocation, &allocated) != VK_SUCCESS) {
        throw std::runtime_error("failed to create buffer '" + info.name + "'");
    }
    setObjectName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<std::uint64_t>(buffer), info.name);
    return std::make_unique<Buffer>(*this, buffer, allocation, info.size, allocated.pMappedData);
}

std::unique_ptr<Image> Device::makeImage(const ImageCreateInfo &info) {
    const VkFormat format = toVkFormat(info.format);
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent = {info.extent.width, info.extent.height, 1};
    imageInfo.mipLevels = info.mipLevels;
    imageInfo.arrayLayers = info.arrayLayers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = toVkImageUsage(info.usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (imageInfo.usage == 0) {
        throw std::logic_error("image '" + info.name + "' has no GPU usage");
    }

    const VmaAllocationCreateInfo allocationInfo = allocationInfoFor(MemoryLocation::DeviceLocal);
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = nullptr;
    if (vmaCreateImage(m_allocator, &imageInfo, &allocationInfo, &image, &allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to create image '" + info.name + "'");
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = info.arrayLayers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange = {aspectMask(info.format), 0, info.mipLevels, 0, info.arrayLayers};
    VkImageView view = VK_NULL_HANDLE;
    if (m_table.vkCreateImageView(m_device, &viewInfo, nullptr, &view) != VK_SUCCESS) {
        vmaDestroyImage(m_allocator, image, allocation);
        throw std::runtime_error("failed to create a view of image '" + info.name + "'");
    }
    setObjectName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<std::uint64_t>(image), info.name);
    setObjectName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<std::uint64_t>(view), info.name);
    return std::make_unique<Image>(*this, info, image, allocation, view);
}

namespace {

VkFormat toVkFormat(VertexAttributeFormat format) {
    switch (format) {
    case VertexAttributeFormat::Float32:   return VK_FORMAT_R32_SFLOAT;
    case VertexAttributeFormat::Float32x2: return VK_FORMAT_R32G32_SFLOAT;
    case VertexAttributeFormat::Float32x3: return VK_FORMAT_R32G32B32_SFLOAT;
    case VertexAttributeFormat::Float32x4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case VertexAttributeFormat::UInt32:    return VK_FORMAT_R32_UINT;
    }
    throw std::runtime_error("Unknown vertex attribute format");
}

} // namespace

std::unique_ptr<GraphicsPipeline> Device::createPipeline(const std::vector<phox::Shader*>& shaders) {
    uint8_t compat_mask = 0;
    std::vector<VkPipelineShaderStageCreateInfo> shader_modules;
    shader_modules.reserve(shaders.size());

    for (auto& shader : shaders) {
        vk::Shader *vulkan_shader = dynamic_cast<vk::Shader*>(shader);
        if (vulkan_shader == nullptr) {
            throw std::runtime_error("createPipeline requires Vulkan shaders");
        }
        const uint8_t shaderBit = static_cast<uint8_t>(1u << (size_t)shader->type);
        if ((compat_mask & shaderBit) != 0) {
            throw std::runtime_error("Duplicate shader types not allowed");
        }
        compat_mask |= shaderBit;
        VkShaderStageFlagBits stage;
        switch (vulkan_shader->type)
        {
            case ShaderType::FRAGMENT:
                stage = VK_SHADER_STAGE_FRAGMENT_BIT;
                break;
            case ShaderType::VERTEX:
                stage = VK_SHADER_STAGE_VERTEX_BIT;
                break;
            default:
                throw std::runtime_error("Invalid shader type provided");
        }
        shader_modules.emplace_back(VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = stage,
            .module = vulkan_shader->module(),
            .pName = "main",
        });
    }
    if ((compat_mask & (1u << (size_t)ShaderType::VERTEX)) == 0 ||
        (compat_mask & (1u << (size_t)ShaderType::FRAGMENT)) == 0) {
        throw std::runtime_error(
            "Graphics pipeline requires vertex and fragment shaders");
    }

    // Everything below is owned by `pipeline` as soon as it is created, so a throw at any
    // point releases whatever exists so far.
    auto pipeline = std::make_unique<GraphicsPipeline>(this);

    // Fixed interface for now: position/normal/uv vertices, one camera UBO (set 0, binding 0),
    // an RGBA albedo push constant, and an offscreen RGBA8 colour target.
    const VertexLayout vertexLayout = VertexLayout::positionNormalTexcoord();
    pipeline->m_descriptorSetLayout = std::make_unique<DescriptorSetLayout>(this, vertexLayout);
    const VkDescriptorSetLayout setLayout = pipeline->m_descriptorSetLayout->handle();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(float) * 4;

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = 1;
    layoutInfo.pSetLayouts = &setLayout;
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    if (m_table.vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &pipeline->m_pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create pipeline layout");
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = VK_FORMAT_R8G8B8A8_UNORM;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    // The render graph transitions the target into and out of COLOR_ATTACHMENT_OPTIMAL itself.
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    if (m_table.vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &pipeline->m_renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass");
    }

    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = vertexLayout.stride();
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    std::vector<VkVertexInputAttributeDescription> attributes;
    for (const auto &attribute : vertexLayout.attributes()) {
        VkVertexInputAttributeDescription desc{};
        desc.location = static_cast<uint32_t>(attributes.size());
        desc.binding = 0;
        desc.format = toVkFormat(attribute.format);
        desc.offset = attribute.offset;
        attributes.push_back(desc);
    }

    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &bindingDescription;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributes.size());
    vertexInput.pVertexAttributeDescriptions = attributes.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    // Viewport/scissor are dynamic so the pipeline is independent of the target size.
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    const VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = 2;
    dynamicState.pDynamicStates = dynamicStates;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &blendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = static_cast<uint32_t>(shader_modules.size());
    pipelineInfo.pStages = shader_modules.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipeline->m_pipelineLayout;
    pipelineInfo.renderPass = pipeline->m_renderPass;
    pipelineInfo.subpass = 0;
    if (m_table.vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                                          &pipeline->m_pipeline) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create graphics pipeline");
    }

    return pipeline;
}

} // namespace phox::vk
