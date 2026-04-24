// =============================================================================
// WestEngine - Scene
// Generic texture asset loading helpers for 2D and cubemap environment maps
// =============================================================================
#include "scene/TextureAssetData.h"

#include "core/Assert.h"
#include "core/Logger.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>

namespace west::scene
{

namespace
{

#pragma pack(push, 1)
struct Ktx2Header
{
    uint8_t identifier[12];
    uint32_t vkFormat = 0;
    uint32_t typeSize = 0;
    uint32_t pixelWidth = 0;
    uint32_t pixelHeight = 0;
    uint32_t pixelDepth = 0;
    uint32_t layerCount = 0;
    uint32_t faceCount = 0;
    uint32_t levelCount = 0;
    uint32_t supercompressionScheme = 0;
    uint32_t dfdByteOffset = 0;
    uint32_t dfdByteLength = 0;
    uint32_t kvdByteOffset = 0;
    uint32_t kvdByteLength = 0;
    uint64_t sgdByteOffset = 0;
    uint64_t sgdByteLength = 0;
};

struct Ktx2LevelIndex
{
    uint64_t byteOffset = 0;
    uint64_t byteLength = 0;
    uint64_t uncompressedByteLength = 0;
};
#pragma pack(pop)

static_assert(sizeof(Ktx2Header) == 80);
static_assert(sizeof(Ktx2LevelIndex) == 24);

constexpr std::array<uint8_t, 12> kKtx2Identifier = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                                      0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
constexpr uint32_t kVkFormatR16G16B16A16Sfloat = 97;
constexpr uint32_t kRGBA16FloatBytesPerPixel = 8;

[[nodiscard]] uint32_t ComputeMipLevelCount(uint32_t width, uint32_t height)
{
    uint32_t levels = 1;
    uint32_t largestDimension = std::max(width, height);
    while (largestDimension > 1)
    {
        largestDimension >>= 1;
        ++levels;
    }
    return levels;
}

[[nodiscard]] float SrgbToLinear(uint8_t value)
{
    const float channel = static_cast<float>(value) / 255.0f;
    if (channel <= 0.04045f)
    {
        return channel / 12.92f;
    }

    return std::pow((channel + 0.055f) / 1.055f, 2.4f);
}

[[nodiscard]] uint8_t LinearToSrgb(float value)
{
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    const float srgb = clamped <= 0.0031308f ? clamped * 12.92f
                                             : (1.055f * std::pow(clamped, 1.0f / 2.4f)) - 0.055f;
    return static_cast<uint8_t>(std::clamp((srgb * 255.0f) + 0.5f, 0.0f, 255.0f));
}

[[nodiscard]] std::vector<uint8_t> BuildNextMipRGBA8(const std::vector<uint8_t>& sourcePixels,
                                                     uint32_t sourceWidth, uint32_t sourceHeight, bool sRGB)
{
    WEST_ASSERT(sourceWidth > 0);
    WEST_ASSERT(sourceHeight > 0);
    WEST_ASSERT(sourcePixels.size() >= static_cast<size_t>(sourceWidth) * sourceHeight * 4ull);

    const uint32_t mipWidth = std::max(1u, sourceWidth >> 1);
    const uint32_t mipHeight = std::max(1u, sourceHeight >> 1);
    std::vector<uint8_t> mipPixels(static_cast<size_t>(mipWidth) * mipHeight * 4ull);

    for (uint32_t y = 0; y < mipHeight; ++y)
    {
        for (uint32_t x = 0; x < mipWidth; ++x)
        {
            const uint32_t sourceX0 = std::min(sourceWidth - 1u, x * 2u);
            const uint32_t sourceY0 = std::min(sourceHeight - 1u, y * 2u);
            const uint32_t sourceX1 = std::min(sourceWidth - 1u, sourceX0 + 1u);
            const uint32_t sourceY1 = std::min(sourceHeight - 1u, sourceY0 + 1u);
            const uint32_t sampleXs[4] = {sourceX0, sourceX1, sourceX0, sourceX1};
            const uint32_t sampleYs[4] = {sourceY0, sourceY0, sourceY1, sourceY1};
            const size_t destinationOffset = (static_cast<size_t>(y) * mipWidth + x) * 4ull;

            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                if (sRGB && channel < 3)
                {
                    float linearSum = 0.0f;
                    for (uint32_t sample = 0; sample < 4; ++sample)
                    {
                        const size_t sourceOffset =
                            (static_cast<size_t>(sampleYs[sample]) * sourceWidth + sampleXs[sample]) * 4ull + channel;
                        linearSum += SrgbToLinear(sourcePixels[sourceOffset]);
                    }
                    mipPixels[destinationOffset + channel] = LinearToSrgb(linearSum * 0.25f);
                    continue;
                }

                uint32_t sum = 0;
                for (uint32_t sample = 0; sample < 4; ++sample)
                {
                    const size_t sourceOffset =
                        (static_cast<size_t>(sampleYs[sample]) * sourceWidth + sampleXs[sample]) * 4ull + channel;
                    sum += sourcePixels[sourceOffset];
                }
                mipPixels[destinationOffset + channel] = static_cast<uint8_t>((sum + 2u) / 4u);
            }
        }
    }

    return mipPixels;
}

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

[[nodiscard]] std::optional<std::vector<uint8_t>> ReadFileBinary(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream.is_open())
    {
        WEST_LOG_WARNING(LogCategory::Scene, "Failed to open texture asset {}", path.string());
        return std::nullopt;
    }

    const std::streamsize size = stream.tellg();
    if (size <= 0)
    {
        WEST_LOG_WARNING(LogCategory::Scene, "Texture asset {} is empty", path.string());
        return std::nullopt;
    }

    stream.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!stream.read(reinterpret_cast<char*>(bytes.data()), size))
    {
        WEST_LOG_WARNING(LogCategory::Scene, "Failed to read texture asset {}", path.string());
        return std::nullopt;
    }

    return bytes;
}

} // namespace

TextureAssetData BuildTexture2DAssetRGBA8(std::string debugName, ImageData image, bool sRGB, bool generateMipChain)
{
    WEST_ASSERT(image.width > 0);
    WEST_ASSERT(image.height > 0);

    const uint64_t expectedBaseBytes = static_cast<uint64_t>(image.width) * image.height * 4ull;
    if (image.pixelsRGBA8.size() < expectedBaseBytes)
    {
        WEST_LOG_WARNING(LogCategory::Scene,
                         "Texture {} has {} bytes, expected at least {}; padding missing texels with white.",
                         debugName, image.pixelsRGBA8.size(), expectedBaseBytes);
        image.pixelsRGBA8.resize(static_cast<size_t>(expectedBaseBytes), 255);
    }
    else if (image.pixelsRGBA8.size() > expectedBaseBytes)
    {
        image.pixelsRGBA8.resize(static_cast<size_t>(expectedBaseBytes));
    }

    TextureAssetData texture{};
    texture.debugName = std::move(debugName);
    texture.width = image.width;
    texture.height = image.height;
    texture.depth = 1;
    texture.mipLevels = generateMipChain ? ComputeMipLevelCount(image.width, image.height) : 1;
    texture.arrayLayers = 1;
    texture.format = sRGB ? rhi::RHIFormat::RGBA8_UNORM_SRGB : rhi::RHIFormat::RGBA8_UNORM;
    texture.dimension = rhi::RHITextureDim::Tex2D;
    texture.subresources.reserve(texture.mipLevels);

    std::vector<uint8_t> mipPixels = std::move(image.pixelsRGBA8);
    uint32_t mipWidth = image.width;
    uint32_t mipHeight = image.height;

    for (uint32_t mipLevel = 0; mipLevel < texture.mipLevels; ++mipLevel)
    {
        const uint64_t sourceOffset = texture.bytes.size();
        texture.bytes.insert(texture.bytes.end(), mipPixels.begin(), mipPixels.end());
        texture.subresources.push_back({
            .sourceOffsetBytes = sourceOffset,
            .rowPitchBytes = mipWidth * 4u,
            .width = mipWidth,
            .height = mipHeight,
            .depth = 1,
            .mipLevel = mipLevel,
            .arrayLayer = 0,
        });

        if (mipWidth == 1 && mipHeight == 1)
        {
            break;
        }

        mipPixels = BuildNextMipRGBA8(mipPixels, mipWidth, mipHeight, sRGB);
        mipWidth = std::max(1u, mipWidth >> 1);
        mipHeight = std::max(1u, mipHeight >> 1);
    }

    texture.mipLevels = static_cast<uint32_t>(texture.subresources.size());
    return texture;
}

std::optional<TextureAssetData> LoadTexture2DAssetRGBA8(const std::filesystem::path& sourcePath, bool sRGB,
                                                        const ImageLoadOptions& options)
{
    std::optional<LoadedImageData> loadedImage = LoadImageRGBA8(sourcePath, options);
    if (!loadedImage.has_value())
    {
        return std::nullopt;
    }

    return BuildTexture2DAssetRGBA8(sourcePath.filename().string(), std::move(loadedImage->image), sRGB);
}

std::optional<TextureAssetData> LoadKtx2CubemapAsset(const std::filesystem::path& sourcePath)
{
    const std::filesystem::path normalizedSource = NormalizePath(sourcePath);
    std::optional<std::vector<uint8_t>> bytes = ReadFileBinary(normalizedSource);
    if (!bytes.has_value())
    {
        return std::nullopt;
    }

    if (bytes->size() < sizeof(Ktx2Header))
    {
        WEST_LOG_WARNING(LogCategory::Scene, "KTX2 asset {} is smaller than the header", normalizedSource.string());
        return std::nullopt;
    }

    Ktx2Header header{};
    std::memcpy(&header, bytes->data(), sizeof(header));
    if (!std::equal(kKtx2Identifier.begin(), kKtx2Identifier.end(), header.identifier))
    {
        WEST_LOG_WARNING(LogCategory::Scene, "Texture asset {} is not a valid KTX2 file", normalizedSource.string());
        return std::nullopt;
    }

    if (header.supercompressionScheme != 0)
    {
        WEST_LOG_WARNING(LogCategory::Scene,
                         "KTX2 asset {} uses unsupported supercompression scheme {}",
                         normalizedSource.string(), header.supercompressionScheme);
        return std::nullopt;
    }

    if (header.vkFormat != kVkFormatR16G16B16A16Sfloat)
    {
        WEST_LOG_WARNING(LogCategory::Scene,
                         "KTX2 asset {} uses unsupported VkFormat {} (expected RGBA16F cubemap)",
                         normalizedSource.string(), header.vkFormat);
        return std::nullopt;
    }

    if (header.pixelWidth == 0 || header.pixelHeight == 0 || header.faceCount != 6 || header.levelCount == 0)
    {
        WEST_LOG_WARNING(LogCategory::Scene,
                         "KTX2 asset {} has unsupported dimensions/faces/levels ({}x{}, faces={}, levels={})",
                         normalizedSource.string(), header.pixelWidth, header.pixelHeight, header.faceCount,
                         header.levelCount);
        return std::nullopt;
    }

    const size_t levelIndexOffset = sizeof(Ktx2Header);
    const size_t levelIndexBytes = static_cast<size_t>(header.levelCount) * sizeof(Ktx2LevelIndex);
    if (bytes->size() < levelIndexOffset + levelIndexBytes)
    {
        WEST_LOG_WARNING(LogCategory::Scene,
                         "KTX2 asset {} is truncated before the level index table",
                         normalizedSource.string());
        return std::nullopt;
    }

    std::vector<Ktx2LevelIndex> levelIndices(header.levelCount);
    std::memcpy(levelIndices.data(), bytes->data() + levelIndexOffset, levelIndexBytes);

    TextureAssetData texture{};
    texture.debugName = normalizedSource.filename().string();
    texture.width = header.pixelWidth;
    texture.height = header.pixelHeight;
    texture.depth = 1;
    texture.mipLevels = header.levelCount;
    texture.arrayLayers = 6;
    texture.format = rhi::RHIFormat::RGBA16_FLOAT;
    texture.dimension = rhi::RHITextureDim::TexCube;
    texture.bytes = std::move(*bytes);
    texture.subresources.reserve(static_cast<size_t>(header.levelCount) * 6ull);

    for (uint32_t mipLevel = 0; mipLevel < header.levelCount; ++mipLevel)
    {
        const Ktx2LevelIndex& level = levelIndices[mipLevel];
        const uint32_t mipWidth = std::max(1u, header.pixelWidth >> mipLevel);
        const uint32_t mipHeight = std::max(1u, header.pixelHeight >> mipLevel);
        const uint64_t faceBytes =
            static_cast<uint64_t>(mipWidth) * static_cast<uint64_t>(mipHeight) * kRGBA16FloatBytesPerPixel;
        const uint64_t requiredLevelBytes = faceBytes * 6ull;

        if (level.byteLength < requiredLevelBytes ||
            level.byteOffset + requiredLevelBytes > static_cast<uint64_t>(texture.bytes.size()))
        {
            WEST_LOG_WARNING(LogCategory::Scene,
                             "KTX2 asset {} has an invalid level table for mip {}",
                             normalizedSource.string(), mipLevel);
            return std::nullopt;
        }

        for (uint32_t faceIndex = 0; faceIndex < 6; ++faceIndex)
        {
            texture.subresources.push_back({
                .sourceOffsetBytes = level.byteOffset + faceBytes * faceIndex,
                .rowPitchBytes = mipWidth * kRGBA16FloatBytesPerPixel,
                .width = mipWidth,
                .height = mipHeight,
                .depth = 1,
                .mipLevel = mipLevel,
                .arrayLayer = faceIndex,
            });
        }
    }

    return texture;
}

} // namespace west::scene
