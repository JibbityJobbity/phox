#ifndef PHOX_VULKAN_GRAPHICS_PIPELINE_HPP
#define PHOX_VULKAN_GRAPHICS_PIPELINE_HPP

#include "phox/pipeline/GraphicsPipeline.hpp"
#include "phox/vulkan/DescriptorSetLayout.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <memory>

namespace phox {

namespace vk {
class Device;

/// Owns the Vulkan objects that make up a graphics pipeline: the pipeline itself, its layout,
/// the descriptor set layout and the render pass it was built against. Created by
/// Device::createPipeline; destroyed in reverse order of creation.
class GraphicsPipeline : public phox::GraphicsPipeline {
    friend class Device;

    private:
        Device* m_device = nullptr;
        std::unique_ptr<DescriptorSetLayout> m_descriptorSetLayout;
        VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
        VkRenderPass m_renderPass = VK_NULL_HANDLE;
        VkPipeline m_pipeline = VK_NULL_HANDLE;

    public:
        explicit GraphicsPipeline(Device* device);
        ~GraphicsPipeline() override;

        VkPipeline handle() const { return m_pipeline; }
        VkPipelineLayout layout() const { return m_pipelineLayout; }
        VkRenderPass renderPass() const { return m_renderPass; }
        VkDescriptorSetLayout descriptorSetLayout() const {
            return m_descriptorSetLayout ? m_descriptorSetLayout->handle() : VK_NULL_HANDLE;
        }
};

}

}

#endif // PHOX_VULKAN_GRAPHICS_PIPELINE_HPP
