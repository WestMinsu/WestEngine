// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan-specific helper macros
// =============================================================================
#pragma once

#include "core/Assert.h"
#include "core/Logger.h"

#include <cstdint>
#include <vulkan/vulkan.h>

// ── VkResult Check Macro ──────────────────────────────────────────────────

#define WEST_VK_CHECK(result)                                                                                          \
    do                                                                                                                 \
    {                                                                                                                  \
        VkResult _vk = (result);                                                                                       \
        WEST_CHECK(_vk == VK_SUCCESS, "VkResult: {}", static_cast<int>(_vk));                                          \
    } while (0)
