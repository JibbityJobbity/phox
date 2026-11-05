#include "States.hpp"

namespace phox::vulkan {

namespace {

constexpr VkPipelineStageFlags2 kShaderStages =
    VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

} // namespace

SyncState syncState(rhi::ResourceState state, bool asSource) {
    using S = rhi::ResourceState;
    switch (state) {
    case S::Undefined:
        if (!asSource) {
            throw std::invalid_argument("barrier: cannot transition to ResourceState::Undefined");
        }
        return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED};
    case S::ColorAttachment:
        return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    case S::DepthWrite:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    case S::DepthRead:
        return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};
    case S::ShaderRead:
        return {kShaderStages, VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    case S::ShaderWrite:
        return {kShaderStages, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case S::TransferSrc:
        return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
    case S::TransferDst:
        return {VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
    case S::VertexOrIndex:
        return {VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT,
                VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_INDEX_READ_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case S::Uniform:
        return {kShaderStages, VK_ACCESS_2_UNIFORM_READ_BIT, VK_IMAGE_LAYOUT_GENERAL};
    case S::Present:
        return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, 0, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
    }
    throw std::invalid_argument("barrier: unknown ResourceState");
}

VkFormat toVk(rhi::Format format) {
    switch (format) {
    case rhi::Format::Undefined: return VK_FORMAT_UNDEFINED;
    case rhi::Format::RGBA8Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case rhi::Format::RGBA8Srgb: return VK_FORMAT_R8G8B8A8_SRGB;
    case rhi::Format::BGRA8Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case rhi::Format::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case rhi::Format::RGBA32Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case rhi::Format::R32Float: return VK_FORMAT_R32_SFLOAT;
    case rhi::Format::D32Float: return VK_FORMAT_D32_SFLOAT;
    case rhi::Format::D24UnormS8Uint: return VK_FORMAT_D24_UNORM_S8_UINT;
    }
    return VK_FORMAT_UNDEFINED;
}

VkImageAspectFlags aspectOf(rhi::Format format) {
    switch (format) {
    case rhi::Format::D32Float: return VK_IMAGE_ASPECT_DEPTH_BIT;
    case rhi::Format::D24UnormS8Uint: return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
    default: return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

VkShaderStageFlagBits toVk(rhi::ShaderStage stage) {
    switch (stage) {
    case rhi::ShaderStage::Vertex: return VK_SHADER_STAGE_VERTEX_BIT;
    case rhi::ShaderStage::Fragment: return VK_SHADER_STAGE_FRAGMENT_BIT;
    case rhi::ShaderStage::Compute: return VK_SHADER_STAGE_COMPUTE_BIT;
    case rhi::ShaderStage::Task: return VK_SHADER_STAGE_TASK_BIT_EXT;
    case rhi::ShaderStage::Mesh: return VK_SHADER_STAGE_MESH_BIT_EXT;
    }
    return VK_SHADER_STAGE_ALL;
}

} // namespace phox::vulkan
