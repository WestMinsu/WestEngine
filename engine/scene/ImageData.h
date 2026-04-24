// =============================================================================
// WestEngine - Scene
// CPU-side image loading helpers for scene textures
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace west::scene
{

struct ImageData
{
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixelsRGBA8;
};

struct ImageLoadOptions
{
    bool enableCache = true;
};

struct ImageLoadStats
{
    bool usedCache = false;
    bool cacheWritten = false;
    double cacheReadMs = 0.0;
    double decodeMs = 0.0;
    double cacheWriteMs = 0.0;
    double totalLoadMs = 0.0;
};

struct LoadedImageData
{
    ImageData image;
    ImageLoadStats stats;
};

[[nodiscard]] std::optional<LoadedImageData> LoadImageRGBA8(const std::filesystem::path& sourcePath,
                                                            const ImageLoadOptions& options = {});

} // namespace west::scene
