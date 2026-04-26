// =============================================================================
// WestEngine - RHI Interface
// Backend-neutral RHI format utilities
// =============================================================================
#pragma once

#include "rhi/interface/RHIEnums.h"

#include <cstdint>

namespace west::rhi
{

[[nodiscard]] inline bool IsBlockCompressedFormat(RHIFormat format) noexcept
{
    switch (format)
    {
    case RHIFormat::BC1_UNORM:
    case RHIFormat::BC1_UNORM_SRGB:
    case RHIFormat::BC3_UNORM:
    case RHIFormat::BC3_UNORM_SRGB:
    case RHIFormat::BC5_UNORM:
    case RHIFormat::BC7_UNORM:
    case RHIFormat::BC7_UNORM_SRGB:
        return true;
    default:
        return false;
    }
}

[[nodiscard]] inline uint32_t GetFormatBlockWidth(RHIFormat format) noexcept
{
    return IsBlockCompressedFormat(format) ? 4u : 1u;
}

[[nodiscard]] inline uint32_t GetFormatBlockHeight(RHIFormat format) noexcept
{
    return IsBlockCompressedFormat(format) ? 4u : 1u;
}

/// Returns the byte size of a single element for the given format.
/// Compressed formats return the block size.
[[nodiscard]] inline uint32_t GetFormatByteSize(RHIFormat format) noexcept
{
    switch (format)
    {
    case RHIFormat::R8_UNORM:
        return 1;
    case RHIFormat::RG8_UNORM:
        return 2;
    case RHIFormat::RGBA8_UNORM:
    case RHIFormat::RGBA8_UNORM_SRGB:
    case RHIFormat::BGRA8_UNORM:
    case RHIFormat::BGRA8_UNORM_SRGB:
        return 4;
    case RHIFormat::R16_FLOAT:
        return 2;
    case RHIFormat::RG16_FLOAT:
        return 4;
    case RHIFormat::RGBA16_FLOAT:
        return 8;
    case RHIFormat::R32_FLOAT:
    case RHIFormat::R32_UINT:
    case RHIFormat::R32_SINT:
        return 4;
    case RHIFormat::RG32_FLOAT:
    case RHIFormat::RG32_UINT:
        return 8;
    case RHIFormat::RGB32_FLOAT:
        return 12;
    case RHIFormat::RGBA32_FLOAT:
    case RHIFormat::RGBA32_UINT:
        return 16;
    case RHIFormat::D16_UNORM:
        return 2;
    case RHIFormat::D32_FLOAT:
        return 4;
    case RHIFormat::D24_UNORM_S8_UINT:
    case RHIFormat::D32_FLOAT_S8_UINT:
        return 4;
    case RHIFormat::BC1_UNORM:
    case RHIFormat::BC1_UNORM_SRGB:
        return 8;
    case RHIFormat::BC3_UNORM:
    case RHIFormat::BC3_UNORM_SRGB:
    case RHIFormat::BC5_UNORM:
    case RHIFormat::BC7_UNORM:
    case RHIFormat::BC7_UNORM_SRGB:
        return 16;
    default:
        return 0;
    }
}

} // namespace west::rhi
