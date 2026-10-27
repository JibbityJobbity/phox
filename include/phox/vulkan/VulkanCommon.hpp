#ifndef PHOX_VULKAN_COMMON_HPP
#define PHOX_VULKAN_COMMON_HPP

#include "volk.h"

// Phox targets Vulkan 1.3 (synchronization2 and dynamic rendering are core there).
#define PHOX_VULKAN_API_VERSION VK_API_VERSION_1_3
#define VMA_VULKAN_VERSION 1003000
#include "vk_mem_alloc.h"

#endif // PHOX_VULKAN_COMMON_HPP