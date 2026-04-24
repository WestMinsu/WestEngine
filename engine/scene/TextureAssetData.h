// =============================================================================
// WestEngine - Scene
// Generic texture asset loading helpers for 2D and cubemap environment maps
// =============================================================================
#pragma once

#include "rhi/interface/RHIEnums.h"
#include "scene/ImageData.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace west::scene
{

struct TextureSubresourceData
{
    uint64_t sourceOffsetBytes = 0;
    uint32_t rowPitchBytes = 0;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t mipLevel = 0;
    uint32_t arrayLayer = 0;
};

struct TextureAssetData
{
    std::string debugName;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    rhi::RHIFormat format = rhi::RHIFormat::Unknown;
    rhi::RHITextureDim dimension = rhi::RHITextureDim::Tex2D;
    std::vector<uint8_t> bytes;
    std::vector<TextureSubresourceData> subresources;
};

[[nodiscard]] TextureAssetData BuildTexture2DAssetRGBA8(std::string debugName, ImageData image, bool sRGB,
                                                        bool generateMipChain = true);

[[nodiscard]] std::optional<TextureAssetData> LoadTexture2DAssetRGBA8(
    const std::filesystem::path& sourcePath, bool sRGB, const ImageLoadOptions& options = {});

[[nodiscard]] std::optional<TextureAssetData> LoadKtx2CubemapAsset(const std::filesystem::path& sourcePath);

} // namespace west::scene
