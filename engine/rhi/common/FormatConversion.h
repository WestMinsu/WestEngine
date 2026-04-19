// =============================================================================
// WestEngine - RHI Common
// Format conversion utilities — RHIFormat ↔ DXGI_FORMAT / VkFormat
// =============================================================================
#pragma once

#include "rhi/interface/RHIEnums.h"

#include <cstdint>

namespace west::rhi
{

/// Returns the byte size of a single element for the given format.
/// Compressed formats return the block size.
uint32_t GetFormatByteSize(RHIFormat format);

/// Convert RHIFormat → DXGI_FORMAT (DX12).
uint32_t ToDXGIFormat(RHIFormat format);

/// Convert RHIFormat → VkFormat (Vulkan).
uint32_t ToVkFormat(RHIFormat format);

} // namespace west::rhi
