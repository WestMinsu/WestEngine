// =============================================================================
// RHI Interface
// All enumeration types and fundamental type aliases for the RHI layer
// =============================================================================
#pragma once

#include "core/Types.h"

namespace west::rhi
{

// ── Graphics API Selection ────────────────────────────────────────────────

enum class RHIBackend : uint8
{
    Vulkan,
    DX12,
    // Metal, // Future expansion slot
};

// ── Resource Format ───────────────────────────────────────────────────────

enum class RHIFormat : uint16
{
    Unknown = 0,

    // Color - Unorm
    R8_UNORM,
    RG8_UNORM,
    RGBA8_UNORM,
    RGBA8_UNORM_SRGB,
    BGRA8_UNORM,
    BGRA8_UNORM_SRGB,

    // Color - Float
    R16_FLOAT,
    RG16_FLOAT,
    RGBA16_FLOAT,
    R32_FLOAT,
    RG32_FLOAT,
    RGB32_FLOAT,
    RGBA32_FLOAT,

    // Color - Uint / Sint
    R32_UINT,
    R32_SINT,
    RG32_UINT,
    RGBA32_UINT,

    // Depth / Stencil
    D16_UNORM,
    D32_FLOAT,
    D24_UNORM_S8_UINT,
    D32_FLOAT_S8_UINT,

    // Compressed (Block Compression)
    BC1_UNORM,
    BC1_UNORM_SRGB,
    BC3_UNORM,
    BC3_UNORM_SRGB,
    BC5_UNORM,
    BC7_UNORM,
    BC7_UNORM_SRGB,

    Count,
};

// ── Queue Type ────────────────────────────────────────────────────────────

enum class RHIQueueType : uint8
{
    Graphics, // Graphics + Compute + Copy
    Compute,  // Async Compute
    Copy,     // Transfer / DMA
};

inline constexpr uint32 kRHIQueueTypeCount = 3;

[[nodiscard]] constexpr uint32 QueueTypeIndex(RHIQueueType queueType)
{
    return static_cast<uint32>(queueType);
}

enum class RHIBindlessBufferView : uint8
{
    ReadOnly,
    ReadWrite,
};

// ── Buffer Usage Flags ────────────────────────────────────────────────────

enum class RHIBufferUsage : uint32
{
    None = 0,
    VertexBuffer = 1 << 0,
    IndexBuffer = 1 << 1,
    ConstantBuffer = 1 << 2,
    StorageBuffer = 1 << 3, // UAV / SSBO
    IndirectArgs = 1 << 4,
    CopySource = 1 << 5,
    CopyDest = 1 << 6,
    AccelStructure = 1 << 7, // Ray Tracing
};
WEST_ENUM_FLAGS(RHIBufferUsage);

// ── Texture Usage Flags ───────────────────────────────────────────────────

enum class RHITextureUsage : uint32
{
    None = 0,
    ShaderResource = 1 << 0,  // SRV / Sampled
    UnorderedAccess = 1 << 1, // UAV / Storage
    RenderTarget = 1 << 2,
    DepthStencil = 1 << 3,
    CopySource = 1 << 4,
    CopyDest = 1 << 5,
    Present = 1 << 6, // SwapChain
};
WEST_ENUM_FLAGS(RHITextureUsage);

// ── Memory Type ───────────────────────────────────────────────────────────

enum class RHIMemoryType : uint8
{
    GPULocal,  // DEVICE_LOCAL (VRAM)
    Upload,    // HOST_VISIBLE | HOST_COHERENT (CPU → GPU)
    Readback,  // HOST_VISIBLE | HOST_CACHED   (GPU → CPU)
    GPUShared, // DEVICE_LOCAL | HOST_VISIBLE   (ReBAR/SAM)
};

// ── Texture Dimension ─────────────────────────────────────────────────────

enum class RHITextureDim : uint8
{
    Tex1D,
    Tex2D,
    Tex3D,
    TexCube,
};

// ── Resource State (Barrier) ──────────────────────────────────────────────

enum class RHIResourceState : uint32
{
    Undefined = 0,
    Common = 1 << 0,
    VertexBuffer = 1 << 1,
    IndexBuffer = 1 << 2,
    ConstantBuffer = 1 << 3,
    ShaderResource = 1 << 4,
    UnorderedAccess = 1 << 5,
    RenderTarget = 1 << 6,
    DepthStencilWrite = 1 << 7,
    DepthStencilRead = 1 << 8,
    CopySource = 1 << 9,
    CopyDest = 1 << 10,
    Present = 1 << 11,
    IndirectArgument = 1 << 12,
    AccelStructRead = 1 << 13,
    AccelStructWrite = 1 << 14,
};
WEST_ENUM_FLAGS(RHIResourceState);

// ── Fixed-Function Pipeline Enums ─────────────────────────────────────────

enum class RHIPrimitiveTopology : uint8
{
    TriangleList,
    TriangleStrip,
    LineList,
    PointList,
};

enum class RHICompareOp : uint8
{
    Never,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always,
};

enum class RHICullMode : uint8
{
    None,
    Front,
    Back
};
enum class RHIFillMode : uint8
{
    Solid,
    Wireframe
};

enum class RHIBlendFactor : uint8
{
    Zero,
    One,
    SrcAlpha,
    OneMinusSrcAlpha,
    DstAlpha,
    OneMinusDstAlpha,
    SrcColor,
    OneMinusSrcColor,
    DstColor,
    OneMinusDstColor,
};

enum class RHIBlendOp : uint8
{
    Add,
    Subtract,
    RevSubtract,
    Min,
    Max
};

enum class RHILoadOp : uint8
{
    Load,
    Clear,
    DontCare
};
enum class RHIStoreOp : uint8
{
    Store,
    DontCare
};

// ── Sampler Enums ─────────────────────────────────────────────────────────

enum class RHIFilter : uint8
{
    Nearest,
    Linear
};
enum class RHIAddressMode : uint8
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder
};
enum class RHIMipmapMode : uint8
{
    Nearest,
    Linear
};
enum class RHIBorderColor : uint8
{
    TransparentBlack,
    OpaqueBlack,
    OpaqueWhite
};

// ── Bindless Resource Index ───────────────────────────────────────────────

using BindlessIndex = uint32;
constexpr BindlessIndex kInvalidBindlessIndex = ~0u;

} // namespace west::rhi
