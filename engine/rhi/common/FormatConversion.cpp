// =============================================================================
// WestEngine - RHI Common
// Format conversion utilities implementation
// =============================================================================
#include "rhi/common/FormatConversion.h"

namespace west::rhi
{

// DXGI_FORMAT values (from dxgiformat.h) — using raw integers to avoid header dependency
uint32_t ToDXGIFormat(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::R8_UNORM:
        return 61;  // DXGI_FORMAT_R8_UNORM
    case RHIFormat::RG8_UNORM:
        return 49;  // DXGI_FORMAT_R8G8_UNORM
    case RHIFormat::RGBA8_UNORM:
        return 28;  // DXGI_FORMAT_R8G8B8A8_UNORM
    case RHIFormat::RGBA8_UNORM_SRGB:
        return 29;  // DXGI_FORMAT_R8G8B8A8_UNORM_SRGB
    case RHIFormat::BGRA8_UNORM:
        return 87;  // DXGI_FORMAT_B8G8R8A8_UNORM
    case RHIFormat::BGRA8_UNORM_SRGB:
        return 91;  // DXGI_FORMAT_B8G8R8A8_UNORM_SRGB
    case RHIFormat::R16_FLOAT:
        return 54;  // DXGI_FORMAT_R16_FLOAT
    case RHIFormat::RG16_FLOAT:
        return 34;  // DXGI_FORMAT_R16G16_FLOAT
    case RHIFormat::RGBA16_FLOAT:
        return 10;  // DXGI_FORMAT_R16G16B16A16_FLOAT
    case RHIFormat::R32_FLOAT:
        return 41;  // DXGI_FORMAT_R32_FLOAT
    case RHIFormat::RG32_FLOAT:
        return 16;  // DXGI_FORMAT_R32G32_FLOAT
    case RHIFormat::RGB32_FLOAT:
        return 6;   // DXGI_FORMAT_R32G32B32_FLOAT
    case RHIFormat::RGBA32_FLOAT:
        return 2;   // DXGI_FORMAT_R32G32B32A32_FLOAT
    case RHIFormat::R32_UINT:
        return 42;  // DXGI_FORMAT_R32_UINT
    case RHIFormat::R32_SINT:
        return 43;  // DXGI_FORMAT_R32_SINT
    case RHIFormat::RG32_UINT:
        return 17;  // DXGI_FORMAT_R32G32_UINT
    case RHIFormat::RGBA32_UINT:
        return 3;   // DXGI_FORMAT_R32G32B32A32_UINT
    case RHIFormat::D16_UNORM:
        return 55;  // DXGI_FORMAT_D16_UNORM
    case RHIFormat::D32_FLOAT:
        return 40;  // DXGI_FORMAT_D32_FLOAT
    case RHIFormat::D24_UNORM_S8_UINT:
        return 45;  // DXGI_FORMAT_D24_UNORM_S8_UINT
    case RHIFormat::D32_FLOAT_S8_UINT:
        return 20;  // DXGI_FORMAT_D32_FLOAT_S8X24_UINT
    default:
        return 0;   // DXGI_FORMAT_UNKNOWN
    }
}

// VkFormat values (from vulkan_core.h)
uint32_t ToVkFormat(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::R8_UNORM:
        return 9;   // VK_FORMAT_R8_UNORM
    case RHIFormat::RG8_UNORM:
        return 16;  // VK_FORMAT_R8G8_UNORM
    case RHIFormat::RGBA8_UNORM:
        return 37;  // VK_FORMAT_R8G8B8A8_UNORM
    case RHIFormat::RGBA8_UNORM_SRGB:
        return 43;  // VK_FORMAT_R8G8B8A8_SRGB
    case RHIFormat::BGRA8_UNORM:
        return 44;  // VK_FORMAT_B8G8R8A8_UNORM
    case RHIFormat::BGRA8_UNORM_SRGB:
        return 50;  // VK_FORMAT_B8G8R8A8_SRGB
    case RHIFormat::R16_FLOAT:
        return 76;  // VK_FORMAT_R16_SFLOAT
    case RHIFormat::RG16_FLOAT:
        return 83;  // VK_FORMAT_R16G16_SFLOAT
    case RHIFormat::RGBA16_FLOAT:
        return 97;  // VK_FORMAT_R16G16B16A16_SFLOAT
    case RHIFormat::R32_FLOAT:
        return 100; // VK_FORMAT_R32_SFLOAT
    case RHIFormat::RG32_FLOAT:
        return 103; // VK_FORMAT_R32G32_SFLOAT
    case RHIFormat::RGB32_FLOAT:
        return 106; // VK_FORMAT_R32G32B32_SFLOAT
    case RHIFormat::RGBA32_FLOAT:
        return 109; // VK_FORMAT_R32G32B32A32_SFLOAT
    case RHIFormat::R32_UINT:
        return 98;  // VK_FORMAT_R32_UINT
    case RHIFormat::R32_SINT:
        return 99;  // VK_FORMAT_R32_SINT
    case RHIFormat::RG32_UINT:
        return 101; // VK_FORMAT_R32G32_UINT
    case RHIFormat::RGBA32_UINT:
        return 107; // VK_FORMAT_R32G32B32A32_UINT
    case RHIFormat::D16_UNORM:
        return 124; // VK_FORMAT_D16_UNORM
    case RHIFormat::D32_FLOAT:
        return 126; // VK_FORMAT_D32_SFLOAT
    case RHIFormat::D24_UNORM_S8_UINT:
        return 129; // VK_FORMAT_D24_UNORM_S8_UINT
    case RHIFormat::D32_FLOAT_S8_UINT:
        return 130; // VK_FORMAT_D32_SFLOAT_S8_UINT
    default:
        return 0;   // VK_FORMAT_UNDEFINED
    }
}

} // namespace west::rhi
