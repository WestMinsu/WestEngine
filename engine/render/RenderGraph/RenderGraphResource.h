// =============================================================================
// WestEngine - Render
// Render Graph resource handles and compile-time metadata
// =============================================================================
#pragma once

#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHITexture.h"
#include "rhi/interface/RHIDescriptors.h"
#include "rhi/interface/RHIEnums.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace west::render
{

inline constexpr uint32_t kInvalidRenderGraphIndex = std::numeric_limits<uint32_t>::max();

enum class ResourceKind : uint8_t
{
    Texture,
    Buffer,
};

enum class ResourceAccessType : uint8_t
{
    Read,
    Write,
    ReadWrite,
};

struct TextureHandle
{
    uint32_t index = kInvalidRenderGraphIndex;

    [[nodiscard]] bool IsValid() const
    {
        return index != kInvalidRenderGraphIndex;
    }
};

struct BufferHandle
{
    uint32_t index = kInvalidRenderGraphIndex;

    [[nodiscard]] bool IsValid() const
    {
        return index != kInvalidRenderGraphIndex;
    }
};

struct ResourceUse
{
    ResourceKind resourceKind = ResourceKind::Texture;
    uint32_t resourceIndex = kInvalidRenderGraphIndex;
    rhi::RHIResourceState state = rhi::RHIResourceState::Common;
    ResourceAccessType accessType = ResourceAccessType::Read;
    rhi::RHIPipelineStage stageMask = rhi::RHIPipelineStage::Auto;
};

struct ResourceLifetime
{
    uint32_t firstUsePass = kInvalidRenderGraphIndex;
    uint32_t lastUsePass = 0;

    [[nodiscard]] bool IsValid() const
    {
        return firstUsePass != kInvalidRenderGraphIndex;
    }
};

struct ResourceAliasInfo
{
    uint32_t slot = kInvalidRenderGraphIndex;
    uint32_t previousResourceIndex = kInvalidRenderGraphIndex;
};

struct RenderGraphResourceInfo
{
    ResourceKind kind = ResourceKind::Texture;
    bool imported = false;
    std::string debugName;

    rhi::RHITextureDesc textureDesc{};
    rhi::RHIBufferDesc bufferDesc{};

    rhi::IRHITexture* importedTexture = nullptr;
    rhi::IRHIBuffer* importedBuffer = nullptr;

    rhi::RHIResourceState initialState = rhi::RHIResourceState::Undefined;
    rhi::RHIResourceState finalState = rhi::RHIResourceState::Common;

    ResourceLifetime lifetime{};
    ResourceAliasInfo alias{};
    uint64_t estimatedSizeBytes = 0;
};

class RenderGraphPass;

struct RenderGraphPassNode
{
    RenderGraphPass* pass = nullptr;
    std::vector<ResourceUse> uses;
};

} // namespace west::render
