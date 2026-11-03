#ifndef PHOX_VULKAN_PIPELINE_CACHE_HPP
#define PHOX_VULKAN_PIPELINE_CACHE_HPP

#include "phox/pipeline/Pipeline.hpp"
#include "phox/pipeline/PipelineDesc.hpp"
#include "phox/vulkan/VulkanCommon.hpp"

#include <array>
#include <memory>
#include <unordered_map>
#include <vector>

namespace phox::vk {

class Device;
class ShaderLibrary;

/// A graphics pipeline built for dynamic rendering, or a compute pipeline. Its layout belongs to the
/// PipelineCache.
class Pipeline final : public phox::Pipeline {
public:
    Pipeline(Device &device, PipelineDesc desc, VkPipeline pipeline, VkPipelineLayout layout);
    ~Pipeline() override;

    VkPipeline handle() const { return m_pipeline; }
    VkPipelineLayout layout() const { return m_layout; }

private:
    Device &m_device;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
};

/// Creates pipelines from descriptions on first use and keeps them. Also owns the descriptor set
/// layouts named by SetLayout and the pipeline layouts built from them.
class PipelineCache {
public:
    PipelineCache(Device &device, ShaderLibrary &shaders);
    ~PipelineCache();
    PipelineCache(const PipelineCache &) = delete;
    PipelineCache &operator=(const PipelineCache &) = delete;

    /// The pipeline for @p desc, created if this is the first request for it.
    /// @throws std::runtime_error if a shader is missing or creation fails
    const Pipeline &get(const PipelineDesc &desc);
    /// @throws std::runtime_error for the task shader layouts on devices without mesh shaders
    VkDescriptorSetLayout setLayout(SetLayout layout) const;
    std::size_t size() const { return m_pipelines.size(); }

private:
    struct LayoutEntry {
        std::vector<SetLayout> sets;
        std::vector<PushConstantRange> pushConstants;
        VkPipelineLayout layout = VK_NULL_HANDLE;
    };

    VkPipelineLayout pipelineLayout(const PipelineDesc &desc);
    std::unique_ptr<Pipeline> create(const PipelineDesc &desc);
    std::unique_ptr<Pipeline> createCompute(const PipelineDesc &desc);

    Device &m_device;
    ShaderLibrary &m_shaders;
    VkSampler m_linearClamp = VK_NULL_HANDLE;
    VkSampler m_nearestClamp = VK_NULL_HANDLE;
    std::array<VkDescriptorSetLayout, kSetLayoutCount> m_setLayouts{};
    std::vector<LayoutEntry> m_layouts;
    std::unordered_map<PipelineDesc, std::unique_ptr<Pipeline>, PipelineDescHash> m_pipelines;
};

} // namespace phox::vk

#endif // PHOX_VULKAN_PIPELINE_CACHE_HPP
