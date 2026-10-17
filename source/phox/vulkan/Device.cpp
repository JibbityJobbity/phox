#define VMA_IMPLEMENTATION

#include "phox/vulkan/Device.hpp"

#include "phox/vulkan/pipeline/GraphicsPipeline.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

using namespace std;

namespace phox {

namespace vk {

VkPhysicalDevice Device::pickPhysicalDevice(const DeviceType type = DeviceType::Discrete) {
    uint32_t nPhysDev;
    vkEnumeratePhysicalDevices(m_instance, &nPhysDev, nullptr);
    auto physDevs = vector<VkPhysicalDevice>(nPhysDev);
    vkEnumeratePhysicalDevices(m_instance, &nPhysDev, physDevs.data());

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

    return out;
}

void Device::Initialize(VkInstance instance, VkPhysicalDevice phys_dev,
                        VkSurfaceKHR surface) {
    m_instance = instance;
    m_physicalDevice = phys_dev;
    m_surface = surface;
    VkResult result;
    // Pick queue families
    uint32_t nQueueFamilies = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(phys_dev, &nQueueFamilies,
                                             nullptr);
    auto *queueFamilies = new VkQueueFamilyProperties[nQueueFamilies];
    vkGetPhysicalDeviceQueueFamilyProperties(phys_dev, &nQueueFamilies,
                                             queueFamilies);
    uint32_t graphicsFamily = UINT32_MAX;
    uint32_t presentFamily = UINT32_MAX;
    VkBool32 presentSupport = VK_FALSE;
    for (uint32_t i = 0; i < nQueueFamilies; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            graphicsFamily = i;
        }
        if (m_surface != VK_NULL_HANDLE) {
            vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, i, m_surface,
                                                 &presentSupport);
            if (presentSupport) {
                presentFamily = i;
            }
        }
    }

    delete[] queueFamilies;
    if (graphicsFamily == UINT32_MAX) {
        throw std::runtime_error("Failed to find graphics queue family");
    }
    if (presentFamily == UINT32_MAX) {
        // Allow headless execution paths that don't create a present surface yet.
        presentFamily = graphicsFamily;
    }

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

        std::vector<VkDeviceQueueCreateInfo> queueInfos;
        queueInfos.push_back(graphicsQueueCreateInfo);
        if (presentFamily != graphicsFamily) {
            queueInfos.push_back(presentQueueCreateInfo);
        }

        VkPhysicalDeviceFeatures deviceFeatures = {};
        vkGetPhysicalDeviceFeatures(m_physicalDevice, &deviceFeatures);
        // TODO: Enable required features

        VkDeviceCreateInfo deviceCreateInfo = {};
        deviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        deviceCreateInfo.pQueueCreateInfos = queueInfos.data();
        deviceCreateInfo.queueCreateInfoCount = queueInfos.size();
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

    {
        m_table.vkGetDeviceQueue(m_device, graphicsFamily, 0,
                                 &m_queueInfo.graphicsQueue);
        m_table.vkGetDeviceQueue(m_device, presentFamily, 0,
                                 &m_queueInfo.presentQueue);
        m_queueInfo.graphicsQueueIndex = graphicsFamily;
        m_queueInfo.presentQueueIndex = presentFamily;
    }

    // Memory allocator
    {
        VmaVulkanFunctions vulkanFunctions = {};
        vulkanFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
        vulkanFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
        vulkanFunctions.vkGetPhysicalDeviceProperties =
            vkGetPhysicalDeviceProperties;
        vulkanFunctions.vkGetPhysicalDeviceMemoryProperties =
            vkGetPhysicalDeviceMemoryProperties;
        vulkanFunctions.vkAllocateMemory = m_table.vkAllocateMemory;
        vulkanFunctions.vkFreeMemory = m_table.vkFreeMemory;
        vulkanFunctions.vkMapMemory = m_table.vkMapMemory;
        vulkanFunctions.vkUnmapMemory = m_table.vkUnmapMemory;
        vulkanFunctions.vkFlushMappedMemoryRanges =
            m_table.vkFlushMappedMemoryRanges;
        vulkanFunctions.vkInvalidateMappedMemoryRanges =
            m_table.vkInvalidateMappedMemoryRanges;
        vulkanFunctions.vkBindBufferMemory = m_table.vkBindBufferMemory;
        vulkanFunctions.vkBindImageMemory = m_table.vkBindImageMemory;
        vulkanFunctions.vkGetBufferMemoryRequirements =
            m_table.vkGetBufferMemoryRequirements;
        vulkanFunctions.vkGetImageMemoryRequirements =
            m_table.vkGetImageMemoryRequirements;
        vulkanFunctions.vkCreateBuffer = m_table.vkCreateBuffer;
        vulkanFunctions.vkDestroyBuffer = m_table.vkDestroyBuffer;
        vulkanFunctions.vkCreateImage = m_table.vkCreateImage;
        vulkanFunctions.vkDestroyImage = m_table.vkDestroyImage;
        vulkanFunctions.vkCmdCopyBuffer = m_table.vkCmdCopyBuffer;
        // Vulkan 1.1 core entry points; VMA reads them through its *2KHR slots.
        vulkanFunctions.vkGetBufferMemoryRequirements2KHR =
            m_table.vkGetBufferMemoryRequirements2;
        vulkanFunctions.vkGetImageMemoryRequirements2KHR =
            m_table.vkGetImageMemoryRequirements2;
        vulkanFunctions.vkBindBufferMemory2KHR = m_table.vkBindBufferMemory2;
        vulkanFunctions.vkBindImageMemory2KHR = m_table.vkBindImageMemory2;
        vulkanFunctions.vkGetPhysicalDeviceMemoryProperties2KHR =
            vkGetPhysicalDeviceMemoryProperties2;

        VmaAllocatorCreateInfo allocatorInfo = {};
        allocatorInfo.physicalDevice = m_physicalDevice;
        allocatorInfo.device = m_device;
        allocatorInfo.instance = m_instance;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2; // must match the instance
        allocatorInfo.pVulkanFunctions = &vulkanFunctions;
        result = vmaCreateAllocator(&allocatorInfo, &m_allocator);

        if (result != VK_SUCCESS) {
            throw std::runtime_error("Failed to create memory allocator!");
        }
    }

    m_initialized = true;
}

void Device::cleanup() {
    if (m_device != VK_NULL_HANDLE && m_table.vkDeviceWaitIdle != nullptr) {
        m_table.vkDeviceWaitIdle(m_device);
    }

    if (m_allocator != VK_NULL_HANDLE) {
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
    }

    if (m_device != VK_NULL_HANDLE) {
        if (m_table.vkDestroyDevice != nullptr) {
            m_table.vkDestroyDevice(m_device, nullptr);
        }
        m_queueInfo.graphicsQueue = VK_NULL_HANDLE;
        m_queueInfo.presentQueue = VK_NULL_HANDLE;
        m_device = VK_NULL_HANDLE;
    }
    // Device doesn't own these
    m_physicalDevice = VK_NULL_HANDLE;
    m_instance = VK_NULL_HANDLE;
    m_initialized = false;
}

bool Device::isInitialized() { return m_initialized; }

Device::~Device() { cleanup(); }

std::unique_ptr<phox::Buffer> Device::allocateBuffer(const size_t size,
                       const MemoryLocation memoryLocation = MemoryLocation::Auto) {
    const VmaMemoryUsage memLocationToUsage[] = {
        VMA_MEMORY_USAGE_AUTO,
        VMA_MEMORY_USAGE_GPU_ONLY,
        VMA_MEMORY_USAGE_CPU_ONLY,
        VMA_MEMORY_USAGE_CPU_COPY,
    };

    VkBuffer buf;
    VmaAllocation allocation;

    uint32_t queueFamilyIndices[] = {m_queueInfo.presentQueueIndex,
                                     m_queueInfo.graphicsQueueIndex};

    VkBufferCreateInfo bufferCreateInfo{};
    bufferCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferCreateInfo.pNext = nullptr;
    bufferCreateInfo.flags = 0;
    bufferCreateInfo.size = size;
    bufferCreateInfo.usage =
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    // CONCURRENT sharing requires distinct family indices; headless uses one family.
    if (m_queueInfo.presentQueueIndex != m_queueInfo.graphicsQueueIndex) {
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        bufferCreateInfo.queueFamilyIndexCount = 2;
        bufferCreateInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    VmaAllocationCreateInfo allocationCreateInfo{};
    allocationCreateInfo.usage = memLocationToUsage[(size_t)memoryLocation];

    const VkResult createResult =
        vmaCreateBuffer(m_allocator, &bufferCreateInfo, &allocationCreateInfo,
                        &buf, &allocation, nullptr);
    if (createResult != VK_SUCCESS) {
        throw std::runtime_error("Failed to create buffer");
    }

    return std::make_unique<Buffer>(this, size, buf, allocation);
}

std::unique_ptr<phox::Buffer> Device::copyBuffer(phox::Buffer &buffer, MemoryLocation memoryLocation) {
    const size_t size = buffer.getSize();
    auto copyBuffer = allocateBuffer(size, memoryLocation);
    auto* srcBuffer = dynamic_cast<vk::Buffer*>(&buffer);
    auto* dstBuffer = dynamic_cast<vk::Buffer*>(copyBuffer.get());
    if (srcBuffer == nullptr || dstBuffer == nullptr) {
        throw std::runtime_error("copyBuffer requires Vulkan buffers");
    }

    VkCommandPoolCreateInfo poolCreateInfo{};
    poolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolCreateInfo.queueFamilyIndex = m_queueInfo.graphicsQueueIndex;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkResult result = m_table.vkCreateCommandPool(m_device, &poolCreateInfo,
                                                  nullptr, &commandPool);
    if (result != VK_SUCCESS) {
        throw std::runtime_error("Failed to create command pool for copy");
    }

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    result = m_table.vkAllocateCommandBuffers(m_device, &allocInfo,
                                              &commandBuffer);
    if (result != VK_SUCCESS) {
        m_table.vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("Failed to allocate command buffer for copy");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    m_table.vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    m_table.vkCmdCopyBuffer(commandBuffer, srcBuffer->m_buffer, dstBuffer->m_buffer,
                            1, &copyRegion);
    m_table.vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    result = m_table.vkQueueSubmit(m_queueInfo.graphicsQueue, 1, &submitInfo,
                                   VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        m_table.vkDestroyCommandPool(m_device, commandPool, nullptr);
        throw std::runtime_error("Failed to submit buffer copy");
    }
    m_table.vkQueueWaitIdle(m_queueInfo.graphicsQueue);
    m_table.vkDestroyCommandPool(m_device, commandPool, nullptr);

    return copyBuffer;
}

void Device::freeBuffer(phox::Buffer &buffer) { buffer.free(); }

std::unique_ptr<phox::Image> Device::allocateImage(const size_t width, const size_t height) {
    VkResult res;
    VmaAllocation allocation;
    VkImage image = VK_NULL_HANDLE;
    uint32_t queueFamilyIndices[] = {m_queueInfo.presentQueueIndex,
                                     m_queueInfo.graphicsQueueIndex};
    VkImageCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    createInfo.imageType = VK_IMAGE_TYPE_2D;
    createInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    createInfo.extent.width = static_cast<uint32_t>(width);
    createInfo.extent.height = static_cast<uint32_t>(height);
    createInfo.extent.depth = 1;
    createInfo.mipLevels = 1;
    createInfo.arrayLayers = 1;
    createInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    createInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    createInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                       VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                       VK_IMAGE_USAGE_SAMPLED_BIT;
    if (m_queueInfo.presentQueueIndex != m_queueInfo.graphicsQueueIndex) {
        createInfo.sharingMode = VK_SHARING_MODE_CONCURRENT;
        createInfo.queueFamilyIndexCount = 2;
        createInfo.pQueueFamilyIndices = queueFamilyIndices;
    } else {
        createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    createInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocationCreateInfo{};
    allocationCreateInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    res = vmaCreateImage(m_allocator, &createInfo, &allocationCreateInfo,
                         &image, &allocation, nullptr);
    if (res != VK_SUCCESS) {
        throw std::runtime_error("Failed to create image!");
    }

    return std::make_unique<Image>(this, image, allocation);
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

std::unique_ptr<phox::Pipeline> Device::createPipeline(const std::vector<phox::Shader*>& shaders) {
    uint8_t compat_mask = 0;
    vector<VkPipelineShaderStageCreateInfo> shader_modules;
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
                throw runtime_error("Invalid shader type provided");
        }
        shader_modules.emplace_back(VkPipelineShaderStageCreateInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = stage,
            .module = vulkan_shader->getShaderModule(),
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
    // an RGBA albedo push constant, and an offscreen RGBA8 target left in TRANSFER_SRC layout.
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
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

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

const VolkDeviceTable* Device::deviceTable() {
    return &m_table;
}

}

} // namespace phox
