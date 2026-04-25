// =============================================================================
// WestEngine - RHI Interface
// All descriptor structures (Desc, Info, Attachment) for RHI creation/submission
// =============================================================================
#pragma once

#include "rhi/interface/RHIEnums.h"

#include <array>
#include <cstdint>
#include <span>

namespace west::rhi
{

inline constexpr uint32_t kMaxPushConstantSizeBytes = 128;

// ── Forward Declarations ──────────────────────────────────────────────────

class IRHIBuffer;
class IRHITexture;
class IRHIFence;
class IRHISemaphore;
class IRHICommandList;
class IRHIPipeline;

// ── Device Config ─────────────────────────────────────────────────────────

struct RHIDeviceConfig
{
    bool enableValidation = true;              // Validation Layer / Debug Layer
    bool enableDX12GPUBasedValidation = false; // D3D12 GPU-Based Validation; requires enableValidation
    bool enableGPUCrashDiag = true;            // DRED / VK_EXT_device_fault
    uint32_t preferredGPUIndex = UINT32_MAX; // UINT32_MAX = auto-select high-performance GPU
    void* windowHandle = nullptr;   // HWND (Win32)
    uint32_t windowWidth = 1920;
    uint32_t windowHeight = 1080;
};

// ── Device Capabilities ───────────────────────────────────────────────────

struct RHIDeviceCaps
{
    bool supportsRayTracing = false;
    bool supportsResizableBar = false;
    bool supportsMeshShaders = false;
    bool supportsTimestampQueries = false;
    std::array<bool, kRHIQueueTypeCount> supportsTimestampQueriesByQueue{};
    uint32_t maxBindlessResources = 0;
    uint64_t dedicatedVideoMemory = 0; // bytes

    [[nodiscard]] bool SupportsTimestampQueries(RHIQueueType queueType) const
    {
        const uint32_t index = QueueTypeIndex(queueType);
        return index < supportsTimestampQueriesByQueue.size() && supportsTimestampQueriesByQueue[index];
    }
};

// ── Timestamp Query Pool Descriptor ───────────────────────────────────────

struct RHITimestampQueryPoolDesc
{
    uint32_t queryCount = 0;
    RHIQueueType queueType = RHIQueueType::Graphics;
    const char* debugName = nullptr;
};

// ── Buffer Descriptor ─────────────────────────────────────────────────────

struct RHIBufferDesc
{
    uint64_t sizeBytes = 0;
    uint32_t structureByteStride = 0; // Per-element stride (vertex stride, structured buffer element size)
    RHIBufferUsage usage = RHIBufferUsage::None;
    RHIMemoryType memoryType = RHIMemoryType::GPULocal;
    const char* debugName = nullptr;
};

[[nodiscard]] inline bool operator==(const RHIBufferDesc& lhs, const RHIBufferDesc& rhs)
{
    return lhs.sizeBytes == rhs.sizeBytes &&
           lhs.structureByteStride == rhs.structureByteStride &&
           lhs.usage == rhs.usage &&
           lhs.memoryType == rhs.memoryType;
}

[[nodiscard]] inline bool operator!=(const RHIBufferDesc& lhs, const RHIBufferDesc& rhs)
{
    return !(lhs == rhs);
}

// ── Texture Descriptor ────────────────────────────────────────────────────

struct RHITextureDesc
{
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t mipLevels = 1;
    uint32_t arrayLayers = 1;
    RHIFormat format = RHIFormat::RGBA8_UNORM;
    RHITextureUsage usage = RHITextureUsage::ShaderResource;
    RHITextureDim dimension = RHITextureDim::Tex2D;
    const char* debugName = nullptr;
};

[[nodiscard]] inline bool operator==(const RHITextureDesc& lhs, const RHITextureDesc& rhs)
{
    return lhs.width == rhs.width &&
           lhs.height == rhs.height &&
           lhs.depth == rhs.depth &&
           lhs.mipLevels == rhs.mipLevels &&
           lhs.arrayLayers == rhs.arrayLayers &&
           lhs.format == rhs.format &&
           lhs.usage == rhs.usage &&
           lhs.dimension == rhs.dimension;
}

[[nodiscard]] inline bool operator!=(const RHITextureDesc& lhs, const RHITextureDesc& rhs)
{
    return !(lhs == rhs);
}

// ── Sampler Descriptor ────────────────────────────────────────────────────

struct RHISamplerDesc
{
    RHIFilter minFilter = RHIFilter::Linear;
    RHIFilter magFilter = RHIFilter::Linear;
    RHIMipmapMode mipmapMode = RHIMipmapMode::Linear;
    RHIAddressMode addressU = RHIAddressMode::Repeat;
    RHIAddressMode addressV = RHIAddressMode::Repeat;
    RHIAddressMode addressW = RHIAddressMode::Repeat;
    float mipLodBias = 0.0f;
    float minLod = 0.0f;
    float maxLod = 1000.0f;
    bool anisotropyEnable = true;
    float maxAnisotropy = 16.0f;
    RHICompareOp compareOp = RHICompareOp::Never;
    bool compareEnable = false;
    RHIBorderColor borderColor = RHIBorderColor::OpaqueBlack;
    const char* debugName = nullptr;
};

// ── SwapChain Descriptor ──────────────────────────────────────────────────

struct RHISwapChainDesc
{
    void* windowHandle = nullptr; // HWND
    uint32_t width = 1920;
    uint32_t height = 1080;
    RHIFormat format = RHIFormat::RGBA8_UNORM;
    uint32_t bufferCount = 3; // Triple buffering
    bool vsync = false;
};

// ── Vertex Attribute ──────────────────────────────────────────────────────

struct RHIVertexAttribute
{
    const char* semantic; // "POSITION", "NORMAL", "TEXCOORD"
    RHIFormat format;
    uint32_t offset;
};

// ── Blend Attachment ──────────────────────────────────────────────────────

struct RHIBlendAttachment
{
    bool blendEnable = false;
    RHIBlendFactor srcColor = RHIBlendFactor::One;
    RHIBlendFactor dstColor = RHIBlendFactor::Zero;
    RHIBlendOp colorOp = RHIBlendOp::Add;
    RHIBlendFactor srcAlpha = RHIBlendFactor::One;
    RHIBlendFactor dstAlpha = RHIBlendFactor::Zero;
    RHIBlendOp alphaOp = RHIBlendOp::Add;
};

// ── Pipeline Descriptors ──────────────────────────────────────────────────

struct RHIGraphicsPipelineDesc
{
    std::span<const uint8_t> vertexShader;
    std::span<const uint8_t> fragmentShader;
    std::span<const RHIVertexAttribute> vertexAttributes;
    uint32_t vertexStride = 0;

    RHIPrimitiveTopology topology = RHIPrimitiveTopology::TriangleList;
    RHICullMode cullMode = RHICullMode::Back;
    RHIFillMode fillMode = RHIFillMode::Solid;
    RHICompareOp depthCompare = RHICompareOp::Less;
    bool depthWrite = true;
    bool depthTest = true;

    std::span<const RHIFormat> colorFormats;
    RHIFormat depthFormat = RHIFormat::D32_FLOAT;
    std::span<const RHIBlendAttachment> blendAttachments;

    uint32_t pushConstantSizeBytes = 0;
    uint64_t psoHash = 0;
    const char* debugName = nullptr;
};

struct RHIComputePipelineDesc
{
    std::span<const uint8_t> computeShader;
    uint32_t pushConstantSizeBytes = 0;
    uint64_t psoHash = 0;
    const char* debugName = nullptr;
};

// ── Render Pass Attachments ───────────────────────────────────────────────

struct RHIColorAttachment
{
    IRHITexture* texture = nullptr;
    RHILoadOp loadOp = RHILoadOp::Clear;
    RHIStoreOp storeOp = RHIStoreOp::Store;
    float clearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
};

struct RHIDepthAttachment
{
    IRHITexture* texture = nullptr;
    RHILoadOp loadOp = RHILoadOp::Clear;
    RHIStoreOp storeOp = RHIStoreOp::Store;
    float clearDepth = 1.0f;
    uint8_t clearStencil = 0;
};

struct RHIRenderPassDesc
{
    std::span<const RHIColorAttachment> colorAttachments;
    RHIDepthAttachment depthAttachment;
    const char* debugName = nullptr;
};

// ── Barrier Descriptor ────────────────────────────────────────────────────

struct RHIBarrierDesc
{
    enum class Type
    {
        Transition,
        Aliasing,
        UAV
    };

    Type type = Type::Transition;

    // Transition
    IRHITexture* texture = nullptr;
    IRHIBuffer* buffer = nullptr; // texture or buffer, one is valid
    RHIResourceState stateBefore = RHIResourceState::Undefined;
    RHIResourceState stateAfter = RHIResourceState::Common;

    // Aliasing (Render Graph Transient Resource)
    IRHITexture* aliasBefore = nullptr;
    IRHITexture* aliasAfter = nullptr;
};

// ── Copy Region ───────────────────────────────────────────────────────────

struct RHICopyRegion
{
    uint64_t bufferOffset = 0;
    uint32_t bufferRowLength = 0;   // 0 = tightly packed
    uint32_t bufferImageHeight = 0; // 0 = tightly packed
    uint32_t texOffsetX = 0;
    uint32_t texOffsetY = 0;
    uint32_t texOffsetZ = 0;
    uint32_t texWidth = 1;
    uint32_t texHeight = 1;
    uint32_t texDepth = 1;
    uint32_t mipLevel = 0;
    uint32_t arrayLayer = 0;
};

// ── Submit Info ───────────────────────────────────────────────────────────

struct RHITimelineWaitDesc
{
    IRHIFence* fence = nullptr;
    uint64_t value = 0;
};

struct RHITimelineSignalDesc
{
    IRHIFence* fence = nullptr;
    uint64_t value = 0;
};

struct RHISubmitInfo
{
    std::span<IRHICommandList* const> commandLists;
    std::span<const RHITimelineWaitDesc> timelineWaits;
    std::span<const RHITimelineSignalDesc> timelineSignals;
    IRHISemaphore* waitSemaphore = nullptr;   // Binary (Swapchain Acquire)
    IRHISemaphore* signalSemaphore = nullptr; // Binary (Present)
};

} // namespace west::rhi
