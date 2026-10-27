#ifndef PHOX_VULKAN_COMMON_HPP
#define PHOX_VULKAN_COMMON_HPP

#include "volk.h"

// Phox targets Vulkan 1.3 (synchronization2 and dynamic rendering are core there).
#define PHOX_VULKAN_API_VERSION VK_API_VERSION_1_3
#define VMA_VULKAN_VERSION 1003000
// VMA's implementation (compiled in vulkan/Device.cpp) trips -Wextra; it is not Phox's code to fix.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "vk_mem_alloc.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#endif // PHOX_VULKAN_COMMON_HPP