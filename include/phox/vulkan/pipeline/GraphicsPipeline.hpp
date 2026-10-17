#ifndef PHOX_VULKAN_GRAPHICS_PIPELINE_HPP
#define PHOX_VULKAN_GRAPHICS_PIPELINE_HPP

#include "phox/pipeline/GraphicsPipeline.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

namespace phox {

namespace vk {
class Device;

class GraphicsPipeline : public phox::GraphicsPipeline {
    private:
        Device* m_device = nullptr;
        VkPipeline m_pipeline = VK_NULL_HANDLE;

    public:
        GraphicsPipeline(Device* device, VkPipeline pipeline);
        ~GraphicsPipeline();
};

}

}

#endif // PHOX_VULKAN_GRAPHICS_PIPELINE_HPP