#ifndef PHOX_PIPELINE_LAYOUT_HPP
#define PHOX_PIPELINE_LAYOUT_HPP

#include "Common.hpp"
#include <vector>

namespace phox {
class PipelineLayout {
  public:
    void
    setDescriptors(const std::vector<VkDescriptorSetLayoutBinding> &layouts);
    void compile();
    PipelineLayout(VkDevice device);
    VkPipelineLayout getPipelineLayout();

  private:
    VkDescriptorSetLayout m_descriptorSetLayout;
    VkPipelineLayout m_pipelineLayout;
    VkDevice m_device;
};
}; // namespace phox

#endif
