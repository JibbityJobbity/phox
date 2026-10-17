#ifndef PHOX_VK_COMMON_HPP
#define PHOX_VK_COMMON_HPP

#include <volk.h>

#ifndef VMA_VULKAN_VERSION
#define VMA_VULKAN_VERSION 1003000
#endif
#ifndef VMA_STATIC_VULKAN_FUNCTIONS
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#endif
#ifndef VMA_DYNAMIC_VULKAN_FUNCTIONS
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#endif

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wparentheses"
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif
#include <vk_mem_alloc.h>
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "phox/rhi/CommandList.hpp"
#include "phox/rhi/Formats.hpp"

#include <stdexcept>
#include <string>

namespace phox::vulkan {

[[noreturn]] inline void throwVk(const char *what, VkResult result) {
    throw std::runtime_error(std::string("Vulkan: ") + what + " failed (VkResult " + std::to_string(static_cast<int>(result)) + ")");
}

#define PHOX_VK_CHECK(expr)                \
    do {                                   \
        const VkResult phoxResult = (expr); \
        if (phoxResult != VK_SUCCESS) {    \
            ::phox::vulkan::throwVk(#expr, phoxResult); \
        }                                  \
    } while (0)

VkFormat toVk(rhi::Format);
VkImageAspectFlags aspectOf(rhi::Format);
VkShaderStageFlagBits toVk(rhi::ShaderStage);

} // namespace phox::vulkan

#endif // PHOX_VK_COMMON_HPP
