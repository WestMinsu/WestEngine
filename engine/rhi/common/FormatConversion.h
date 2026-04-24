// =============================================================================
// WestEngine - RHI Common
// Format conversion utilities — RHIFormat ↔ DXGI_FORMAT / VkFormat
// =============================================================================
#pragma once

#include "rhi/interface/RHIEnums.h"
#include "rhi/interface/RHIFormatUtils.h"

#include <cstdint>

namespace west::rhi
{

/// Convert RHIFormat → DXGI_FORMAT (DX12).
uint32_t ToDXGIFormat(RHIFormat format);

/// Convert RHIFormat → VkFormat (Vulkan).
uint32_t ToVkFormat(RHIFormat format);

} // namespace west::rhi
