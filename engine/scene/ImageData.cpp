// =============================================================================
// WestEngine - Scene
// CPU-side image loading helpers for scene textures
// =============================================================================
#include "scene/ImageData.h"

#include "core/Logger.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <chrono>
#include <fstream>
#include <format>
#include <string_view>
#include <system_error>

namespace west::scene
{

namespace
{

using Clock = std::chrono::steady_clock;

constexpr uint32_t kImageCacheVersion = 1;

struct ImageCacheHeader
{
    uint32_t version = kImageCacheVersion;
    int64_t sourceWriteTime = 0;
    uint64_t sourceFileSize = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint64_t pixelBytes = 0;
};

[[nodiscard]] std::filesystem::path NormalizePath(const std::filesystem::path& path)
{
    std::error_code errorCode;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, errorCode);
    if (!errorCode)
    {
        return canonical;
    }

    return path.lexically_normal();
}

[[nodiscard]] std::filesystem::path BuildImageCachePath(const std::filesystem::path& sourcePath)
{
    return sourcePath.parent_path() / std::format("{}.westtex.bin", sourcePath.filename().string());
}

[[nodiscard]] int64_t GetLastWriteTimeTicks(const std::filesystem::path& path)
{
    std::error_code errorCode;
    const auto timestamp = std::filesystem::last_write_time(path, errorCode);
    if (errorCode)
    {
        return 0;
    }

    return static_cast<int64_t>(timestamp.time_since_epoch().count());
}

[[nodiscard]] uint64_t GetFileSizeBytes(const std::filesystem::path& path)
{
    std::error_code errorCode;
    const auto fileSize = std::filesystem::file_size(path, errorCode);
    if (errorCode)
    {
        return 0;
    }

    return static_cast<uint64_t>(fileSize);
}

template <typename T>
bool WriteValue(std::ofstream& stream, const T& value)
{
    stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return stream.good();
}

template <typename T>
bool ReadValue(std::ifstream& stream, T& value)
{
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    return stream.good();
}

bool TryReadImageCache(const std::filesystem::path& cachePath, const std::filesystem::path& sourcePath, ImageData& image)
{
    std::ifstream stream(cachePath, std::ios::binary);
    if (!stream.is_open())
    {
        return false;
    }

    ImageCacheHeader header{};
    if (!ReadValue(stream, header))
    {
        return false;
    }

    if (header.version != kImageCacheVersion)
    {
        return false;
    }

    if (header.sourceWriteTime != GetLastWriteTimeTicks(sourcePath) || header.sourceFileSize != GetFileSizeBytes(sourcePath))
    {
        return false;
    }

    const uint64_t expectedPixelBytes = static_cast<uint64_t>(header.width) * static_cast<uint64_t>(header.height) * 4ull;
    if (header.pixelBytes != expectedPixelBytes)
    {
        return false;
    }

    image.width = header.width;
    image.height = header.height;
    image.pixelsRGBA8.resize(static_cast<size_t>(header.pixelBytes));
    if (!image.pixelsRGBA8.empty())
    {
        stream.read(reinterpret_cast<char*>(image.pixelsRGBA8.data()), static_cast<std::streamsize>(image.pixelsRGBA8.size()));
    }
    return stream.good();
}

bool WriteImageCache(const std::filesystem::path& cachePath, const std::filesystem::path& sourcePath, const ImageData& image)
{
    std::ofstream stream(cachePath, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return false;
    }

    const ImageCacheHeader header{
        .version = kImageCacheVersion,
        .sourceWriteTime = GetLastWriteTimeTicks(sourcePath),
        .sourceFileSize = GetFileSizeBytes(sourcePath),
        .width = image.width,
        .height = image.height,
        .pixelBytes = static_cast<uint64_t>(image.pixelsRGBA8.size()),
    };

    if (!WriteValue(stream, header))
    {
        return false;
    }

    if (!image.pixelsRGBA8.empty())
    {
        stream.write(reinterpret_cast<const char*>(image.pixelsRGBA8.data()),
                     static_cast<std::streamsize>(image.pixelsRGBA8.size()));
    }
    return stream.good();
}

std::optional<ImageData> DecodeImageRGBA8(const std::filesystem::path& sourcePath)
{
    stbi_set_flip_vertically_on_load(0);

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(sourcePath.string().c_str(), &width, &height, &channels, STBI_rgb_alpha);
    if (pixels == nullptr)
    {
        WEST_LOG_WARNING(LogCategory::Scene, "Failed to load texture {}: {}", sourcePath.string(),
                         stbi_failure_reason() != nullptr ? stbi_failure_reason() : "unknown");
        return std::nullopt;
    }

    ImageData image{};
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.pixelsRGBA8.assign(pixels, pixels + (static_cast<size_t>(width) * static_cast<size_t>(height) * 4));
    stbi_image_free(pixels);
    return image;
}

} // namespace

std::optional<LoadedImageData> LoadImageRGBA8(const std::filesystem::path& sourcePath, const ImageLoadOptions& options)
{
    const auto totalStart = Clock::now();
    const std::filesystem::path normalizedSource = NormalizePath(sourcePath);
    const std::filesystem::path cachePath = BuildImageCachePath(normalizedSource);

    LoadedImageData loadedImage{};

    if (options.enableCache)
    {
        const auto cacheReadStart = Clock::now();
        if (TryReadImageCache(cachePath, normalizedSource, loadedImage.image))
        {
            loadedImage.stats.usedCache = true;
            loadedImage.stats.cacheReadMs =
                std::chrono::duration<double, std::milli>(Clock::now() - cacheReadStart).count();
            loadedImage.stats.totalLoadMs =
                std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
            return loadedImage;
        }

        loadedImage.stats.cacheReadMs = std::chrono::duration<double, std::milli>(Clock::now() - cacheReadStart).count();
    }

    const auto decodeStart = Clock::now();
    std::optional<ImageData> decodedImage = DecodeImageRGBA8(normalizedSource);
    loadedImage.stats.decodeMs = std::chrono::duration<double, std::milli>(Clock::now() - decodeStart).count();
    if (!decodedImage.has_value())
    {
        loadedImage.stats.totalLoadMs = std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
        return std::nullopt;
    }

    loadedImage.image = std::move(*decodedImage);

    if (options.enableCache)
    {
        const auto cacheWriteStart = Clock::now();
        loadedImage.stats.cacheWritten = WriteImageCache(cachePath, normalizedSource, loadedImage.image);
        loadedImage.stats.cacheWriteMs =
            std::chrono::duration<double, std::milli>(Clock::now() - cacheWriteStart).count();
    }

    loadedImage.stats.totalLoadMs = std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
    return loadedImage;
}

} // namespace west::scene
