// =============================================================================
// WestEngine - RHI DX12
// DX12 command list implementation — Phase 1: barrier + ClearRTV only
// =============================================================================
#include "rhi/dx12/DX12CommandList.h"

#include "rhi/dx12/DX12Buffer.h"
#include "rhi/dx12/DX12Pipeline.h"
#include "rhi/dx12/DX12TimestampQueryPool.h"
#include "rhi/dx12/DX12Texture.h"
#include "rhi/common/FormatConversion.h"

#include <vector>

namespace west::rhi
{

namespace
{

[[nodiscard]] D3D12_RESOURCE_STATES ConvertResourceState(RHIResourceState state)
{
    D3D12_RESOURCE_STATES d3dState = D3D12_RESOURCE_STATE_COMMON;

    if (HasFlag(state, RHIResourceState::RenderTarget))
        d3dState |= D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (HasFlag(state, RHIResourceState::Present))
        d3dState |= D3D12_RESOURCE_STATE_PRESENT;
    if (HasFlag(state, RHIResourceState::ShaderResource))
        d3dState |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (HasFlag(state, RHIResourceState::UnorderedAccess))
        d3dState |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    if (HasFlag(state, RHIResourceState::DepthStencilWrite))
        d3dState |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
    if (HasFlag(state, RHIResourceState::DepthStencilRead))
        d3dState |= D3D12_RESOURCE_STATE_DEPTH_READ;
    if (HasFlag(state, RHIResourceState::CopySource))
        d3dState |= D3D12_RESOURCE_STATE_COPY_SOURCE;
    if (HasFlag(state, RHIResourceState::CopyDest))
        d3dState |= D3D12_RESOURCE_STATE_COPY_DEST;
    if (HasFlag(state, RHIResourceState::VertexBuffer))
        d3dState |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    if (HasFlag(state, RHIResourceState::ConstantBuffer))
        d3dState |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    if (HasFlag(state, RHIResourceState::IndexBuffer))
        d3dState |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
    if (HasFlag(state, RHIResourceState::IndirectArgument))
        d3dState |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    if (HasFlag(state, RHIResourceState::AccelStructRead) || HasFlag(state, RHIResourceState::AccelStructWrite))
        d3dState |= D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;

    return d3dState;
}

[[nodiscard]] bool IsStencilFormat(RHIFormat format)
{
    return format == RHIFormat::D24_UNORM_S8_UINT || format == RHIFormat::D32_FLOAT_S8_UINT;
}

enum class EnhancedBarrierKind
{
    Global,
    Texture,
    Buffer
};

struct EnhancedBarrier
{
    EnhancedBarrierKind kind = EnhancedBarrierKind::Global;
    D3D12_GLOBAL_BARRIER global{};
    D3D12_TEXTURE_BARRIER texture{};
    D3D12_BUFFER_BARRIER buffer{};
};

[[nodiscard]] bool HasD3D12Access(D3D12_BARRIER_ACCESS access, D3D12_BARRIER_ACCESS flag)
{
    return (static_cast<uint32_t>(access) & static_cast<uint32_t>(flag)) != 0;
}

[[nodiscard]] D3D12_BARRIER_ACCESS ConvertBarrierAccess(RHIResourceState state)
{
    if (state == RHIResourceState::Undefined)
    {
        return D3D12_BARRIER_ACCESS_NO_ACCESS;
    }

    D3D12_BARRIER_ACCESS access = D3D12_BARRIER_ACCESS_COMMON;
    bool hasExplicitAccess = false;
    auto addAccess = [&](D3D12_BARRIER_ACCESS flag) {
        access |= flag;
        hasExplicitAccess = true;
    };

    if (HasFlag(state, RHIResourceState::VertexBuffer))
        addAccess(D3D12_BARRIER_ACCESS_VERTEX_BUFFER);
    if (HasFlag(state, RHIResourceState::IndexBuffer))
        addAccess(D3D12_BARRIER_ACCESS_INDEX_BUFFER);
    if (HasFlag(state, RHIResourceState::ConstantBuffer))
        addAccess(D3D12_BARRIER_ACCESS_CONSTANT_BUFFER);
    if (HasFlag(state, RHIResourceState::ShaderResource))
        addAccess(D3D12_BARRIER_ACCESS_SHADER_RESOURCE);
    if (HasFlag(state, RHIResourceState::UnorderedAccess))
        addAccess(D3D12_BARRIER_ACCESS_UNORDERED_ACCESS);
    if (HasFlag(state, RHIResourceState::RenderTarget))
        addAccess(D3D12_BARRIER_ACCESS_RENDER_TARGET);
    if (HasFlag(state, RHIResourceState::DepthStencilWrite))
        addAccess(D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE);
    if (HasFlag(state, RHIResourceState::DepthStencilRead))
        addAccess(D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ);
    if (HasFlag(state, RHIResourceState::CopySource))
        addAccess(D3D12_BARRIER_ACCESS_COPY_SOURCE);
    if (HasFlag(state, RHIResourceState::CopyDest))
        addAccess(D3D12_BARRIER_ACCESS_COPY_DEST);
    if (HasFlag(state, RHIResourceState::IndirectArgument))
        addAccess(D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT);
    if (HasFlag(state, RHIResourceState::AccelStructRead))
        addAccess(D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ);
    if (HasFlag(state, RHIResourceState::AccelStructWrite))
        addAccess(D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE);

    return hasExplicitAccess ? access : D3D12_BARRIER_ACCESS_COMMON;
}

[[nodiscard]] D3D12_BARRIER_LAYOUT ConvertTextureLayout(RHIResourceState state, RHIQueueType queueType)
{
    if (state == RHIResourceState::Undefined)
        return D3D12_BARRIER_LAYOUT_UNDEFINED;
    if (HasFlag(state, RHIResourceState::Present))
        return D3D12_BARRIER_LAYOUT_PRESENT;
    if (HasFlag(state, RHIResourceState::RenderTarget))
        return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    if (HasFlag(state, RHIResourceState::UnorderedAccess))
        return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
    if (HasFlag(state, RHIResourceState::DepthStencilWrite))
        return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;

    const bool depthRead = HasFlag(state, RHIResourceState::DepthStencilRead);
    const bool shaderRead = HasFlag(state, RHIResourceState::ShaderResource);
    const bool copyRead = HasFlag(state, RHIResourceState::CopySource);
    if (depthRead && (shaderRead || copyRead) && queueType == RHIQueueType::Graphics)
        return D3D12_BARRIER_LAYOUT_DIRECT_QUEUE_GENERIC_READ;
    if (depthRead)
        return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_READ;
    if (shaderRead && copyRead)
        return D3D12_BARRIER_LAYOUT_GENERIC_READ;
    if (shaderRead)
        return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    if (copyRead)
        return D3D12_BARRIER_LAYOUT_COPY_SOURCE;
    if (HasFlag(state, RHIResourceState::CopyDest))
        return D3D12_BARRIER_LAYOUT_COPY_DEST;

    return D3D12_BARRIER_LAYOUT_COMMON;
}

[[nodiscard]] bool IsCompressedWriteLayout(D3D12_BARRIER_LAYOUT layout)
{
    return layout == D3D12_BARRIER_LAYOUT_RENDER_TARGET ||
           layout == D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE ||
           layout == D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
}

[[nodiscard]] D3D12_TEXTURE_BARRIER_FLAGS GetTextureTransitionFlags(RHIResourceState stateBefore,
                                                                    D3D12_BARRIER_LAYOUT layoutBefore,
                                                                    D3D12_BARRIER_LAYOUT layoutAfter)
{
    if (stateBefore == RHIResourceState::Undefined &&
        layoutBefore == D3D12_BARRIER_LAYOUT_UNDEFINED &&
        IsCompressedWriteLayout(layoutAfter))
    {
        return D3D12_TEXTURE_BARRIER_FLAG_DISCARD;
    }

    return D3D12_TEXTURE_BARRIER_FLAG_NONE;
}

[[nodiscard]] D3D12_BARRIER_SYNC ConvertPipelineStageMask(RHIPipelineStage stageMask)
{
    if (stageMask == RHIPipelineStage::Auto)
    {
        return D3D12_BARRIER_SYNC_NONE;
    }
    if (HasFlag(stageMask, RHIPipelineStage::AllCommands))
    {
        return D3D12_BARRIER_SYNC_ALL;
    }

    D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;
    if (HasFlag(stageMask, RHIPipelineStage::DrawIndirect))
        sync |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
    if (HasFlag(stageMask, RHIPipelineStage::VertexInput))
        sync |= D3D12_BARRIER_SYNC_INDEX_INPUT | D3D12_BARRIER_SYNC_VERTEX_SHADING;
    if (HasFlag(stageMask, RHIPipelineStage::VertexShader))
        sync |= D3D12_BARRIER_SYNC_VERTEX_SHADING;
    if (HasFlag(stageMask, RHIPipelineStage::PixelShader))
        sync |= D3D12_BARRIER_SYNC_PIXEL_SHADING;
    if (HasFlag(stageMask, RHIPipelineStage::ComputeShader))
        sync |= D3D12_BARRIER_SYNC_COMPUTE_SHADING;
    if (HasFlag(stageMask, RHIPipelineStage::ColorAttachmentOutput))
        sync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
    if (HasFlag(stageMask, RHIPipelineStage::DepthStencil))
        sync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
    if (HasFlag(stageMask, RHIPipelineStage::Copy))
        sync |= D3D12_BARRIER_SYNC_COPY;
    if (HasFlag(stageMask, RHIPipelineStage::AllGraphics))
        sync |= D3D12_BARRIER_SYNC_DRAW;

    return sync;
}

[[nodiscard]] D3D12_BARRIER_SYNC SanitizeSyncForQueue(D3D12_BARRIER_SYNC sync, RHIQueueType queueType)
{
    if (sync == D3D12_BARRIER_SYNC_NONE || sync == D3D12_BARRIER_SYNC_ALL)
    {
        return sync;
    }

    D3D12_BARRIER_SYNC allowed = D3D12_BARRIER_SYNC_ALL | D3D12_BARRIER_SYNC_COPY;
    if (queueType == RHIQueueType::Graphics)
    {
        allowed |= D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_INDEX_INPUT |
                   D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_PIXEL_SHADING |
                   D3D12_BARRIER_SYNC_DEPTH_STENCIL | D3D12_BARRIER_SYNC_RENDER_TARGET |
                   D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING |
                   D3D12_BARRIER_SYNC_EXECUTE_INDIRECT | D3D12_BARRIER_SYNC_ALL_SHADING |
                   D3D12_BARRIER_SYNC_NON_PIXEL_SHADING |
                   D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
                   D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE |
                   D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO |
                   D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
    }
    else if (queueType == RHIQueueType::Compute)
    {
        allowed |= D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_RAYTRACING |
                   D3D12_BARRIER_SYNC_EXECUTE_INDIRECT | D3D12_BARRIER_SYNC_ALL_SHADING |
                   D3D12_BARRIER_SYNC_NON_PIXEL_SHADING |
                   D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
                   D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE |
                   D3D12_BARRIER_SYNC_EMIT_RAYTRACING_ACCELERATION_STRUCTURE_POSTBUILD_INFO |
                   D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW;
    }

    if ((static_cast<uint32_t>(sync) & ~static_cast<uint32_t>(allowed)) != 0)
    {
        return D3D12_BARRIER_SYNC_ALL;
    }

    return sync;
}

[[nodiscard]] D3D12_BARRIER_SYNC DefaultSyncForAccess(D3D12_BARRIER_ACCESS access, RHIQueueType queueType)
{
    if (access == D3D12_BARRIER_ACCESS_NO_ACCESS)
    {
        return D3D12_BARRIER_SYNC_NONE;
    }
    if (access == D3D12_BARRIER_ACCESS_COMMON)
    {
        return D3D12_BARRIER_SYNC_ALL;
    }
    if (queueType == RHIQueueType::Copy)
    {
        return D3D12_BARRIER_SYNC_COPY;
    }

    D3D12_BARRIER_SYNC sync = D3D12_BARRIER_SYNC_NONE;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_VERTEX_BUFFER) ||
        HasD3D12Access(access, D3D12_BARRIER_ACCESS_CONSTANT_BUFFER) ||
        HasD3D12Access(access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE) ||
        HasD3D12Access(access, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS))
    {
        sync |= D3D12_BARRIER_SYNC_ALL_SHADING;
    }
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_INDEX_BUFFER))
        sync |= D3D12_BARRIER_SYNC_INDEX_INPUT;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_RENDER_TARGET))
        sync |= D3D12_BARRIER_SYNC_RENDER_TARGET;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE) ||
        HasD3D12Access(access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ))
        sync |= D3D12_BARRIER_SYNC_DEPTH_STENCIL;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_COPY_DEST) ||
        HasD3D12Access(access, D3D12_BARRIER_ACCESS_COPY_SOURCE))
        sync |= D3D12_BARRIER_SYNC_COPY;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT))
        sync |= D3D12_BARRIER_SYNC_EXECUTE_INDIRECT;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ))
        sync |= D3D12_BARRIER_SYNC_RAYTRACING;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE))
        sync |= D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
                D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE;

    return sync == D3D12_BARRIER_SYNC_NONE ? D3D12_BARRIER_SYNC_ALL : SanitizeSyncForQueue(sync, queueType);
}

[[nodiscard]] bool IsSyncCompatibleWithAccess(D3D12_BARRIER_SYNC sync, D3D12_BARRIER_ACCESS access)
{
    if (sync == D3D12_BARRIER_SYNC_ALL || access == D3D12_BARRIER_ACCESS_NO_ACCESS ||
        access == D3D12_BARRIER_ACCESS_COMMON)
    {
        return true;
    }

    auto anySync = [&](D3D12_BARRIER_SYNC allowed) {
        return (static_cast<uint32_t>(sync) & static_cast<uint32_t>(allowed)) != 0;
    };

    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_VERTEX_BUFFER) &&
        !anySync(D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_ALL_SHADING))
        return false;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_CONSTANT_BUFFER) &&
        !anySync(D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_PIXEL_SHADING |
                 D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_DRAW |
                 D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_NON_PIXEL_SHADING))
        return false;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_INDEX_BUFFER) &&
        !anySync(D3D12_BARRIER_SYNC_INDEX_INPUT | D3D12_BARRIER_SYNC_DRAW))
        return false;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_RENDER_TARGET) &&
        !anySync(D3D12_BARRIER_SYNC_DRAW | D3D12_BARRIER_SYNC_RENDER_TARGET))
        return false;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS) &&
        !anySync(D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_PIXEL_SHADING |
                 D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_ALL_SHADING |
                 D3D12_BARRIER_SYNC_NON_PIXEL_SHADING | D3D12_BARRIER_SYNC_CLEAR_UNORDERED_ACCESS_VIEW))
        return false;
    if ((HasD3D12Access(access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE) ||
         HasD3D12Access(access, D3D12_BARRIER_ACCESS_DEPTH_STENCIL_READ)) &&
        !anySync(D3D12_BARRIER_SYNC_DEPTH_STENCIL | D3D12_BARRIER_SYNC_DRAW))
        return false;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_SHADER_RESOURCE) &&
        !anySync(D3D12_BARRIER_SYNC_VERTEX_SHADING | D3D12_BARRIER_SYNC_PIXEL_SHADING |
                 D3D12_BARRIER_SYNC_COMPUTE_SHADING | D3D12_BARRIER_SYNC_DRAW |
                 D3D12_BARRIER_SYNC_ALL_SHADING | D3D12_BARRIER_SYNC_NON_PIXEL_SHADING |
                 D3D12_BARRIER_SYNC_RAYTRACING))
        return false;
    if ((HasD3D12Access(access, D3D12_BARRIER_ACCESS_COPY_DEST) ||
         HasD3D12Access(access, D3D12_BARRIER_ACCESS_COPY_SOURCE)) &&
        !anySync(D3D12_BARRIER_SYNC_COPY))
        return false;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_INDIRECT_ARGUMENT) &&
        !anySync(D3D12_BARRIER_SYNC_EXECUTE_INDIRECT))
        return false;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_READ) &&
        !anySync(D3D12_BARRIER_SYNC_RAYTRACING | D3D12_BARRIER_SYNC_ALL_SHADING))
        return false;
    if (HasD3D12Access(access, D3D12_BARRIER_ACCESS_RAYTRACING_ACCELERATION_STRUCTURE_WRITE) &&
        !anySync(D3D12_BARRIER_SYNC_BUILD_RAYTRACING_ACCELERATION_STRUCTURE |
                 D3D12_BARRIER_SYNC_COPY_RAYTRACING_ACCELERATION_STRUCTURE))
        return false;

    return true;
}

[[nodiscard]] D3D12_BARRIER_SYNC NormalizeBarrierSync(RHIPipelineStage stageMask, D3D12_BARRIER_ACCESS access,
                                                      RHIQueueType queueType)
{
    if (access == D3D12_BARRIER_ACCESS_NO_ACCESS)
    {
        return D3D12_BARRIER_SYNC_NONE;
    }

    D3D12_BARRIER_SYNC sync = SanitizeSyncForQueue(ConvertPipelineStageMask(stageMask), queueType);
    if (sync == D3D12_BARRIER_SYNC_NONE)
    {
        return DefaultSyncForAccess(access, queueType);
    }
    if (!IsSyncCompatibleWithAccess(sync, access))
    {
        sync |= DefaultSyncForAccess(access, queueType);
        sync = SanitizeSyncForQueue(sync, queueType);
    }

    return sync;
}

[[nodiscard]] D3D12_BARRIER_SUBRESOURCE_RANGE AllTextureSubresources()
{
    D3D12_BARRIER_SUBRESOURCE_RANGE range{};
    range.IndexOrFirstMipLevel = 0xFFFFFFFFu;
    range.NumMipLevels = 0;
    return range;
}

[[nodiscard]] ID3D12Resource* ResolveBarrierResource(const RHIBarrierDesc& desc)
{
    if (desc.texture)
    {
        return static_cast<DX12Texture*>(desc.texture)->GetD3DResource();
    }

    if (desc.buffer)
    {
        return static_cast<DX12Buffer*>(desc.buffer)->GetD3DResource();
    }

    return nullptr;
}

[[nodiscard]] bool TryBuildEnhancedTextureDeactivationBarrier(const RHIBarrierDesc& desc, RHIQueueType queueType,
                                                             EnhancedBarrier& barrier)
{
    if (!desc.aliasBefore || desc.stateBefore == RHIResourceState::Undefined)
    {
        return false;
    }

    auto* beforeTexture = static_cast<DX12Texture*>(desc.aliasBefore);
    ID3D12Resource* beforeResource = beforeTexture->GetD3DResource();
    if (!beforeResource)
    {
        return false;
    }

    const D3D12_BARRIER_ACCESS accessBefore = ConvertBarrierAccess(desc.stateBefore);
    const D3D12_BARRIER_LAYOUT layoutBefore = ConvertTextureLayout(desc.stateBefore, queueType);

    barrier = {};
    barrier.kind = EnhancedBarrierKind::Texture;
    barrier.texture.SyncBefore = NormalizeBarrierSync(desc.srcStageMask, accessBefore, queueType);
    barrier.texture.SyncAfter = D3D12_BARRIER_SYNC_ALL;
    barrier.texture.AccessBefore = accessBefore;
    barrier.texture.AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS;
    barrier.texture.LayoutBefore = layoutBefore;
    barrier.texture.LayoutAfter = D3D12_BARRIER_LAYOUT_UNDEFINED;
    barrier.texture.pResource = beforeResource;
    barrier.texture.Subresources = AllTextureSubresources();
    barrier.texture.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
    return true;
}

[[nodiscard]] bool TryBuildEnhancedD3D12Barrier(const RHIBarrierDesc& desc, RHIQueueType queueType,
                                                EnhancedBarrier& barrier)
{
    barrier = {};

    if (desc.type == RHIBarrierDesc::Type::Transition)
    {
        ID3D12Resource* resource = ResolveBarrierResource(desc);
        if (!resource)
        {
            WEST_LOG_WARNING(LogCategory::RHI, "ResourceBarrier: no valid resource for enhanced transition");
            return false;
        }

        D3D12_BARRIER_ACCESS accessBefore = ConvertBarrierAccess(desc.stateBefore);
        D3D12_BARRIER_ACCESS accessAfter = ConvertBarrierAccess(desc.stateAfter);
        const D3D12_BARRIER_SYNC syncBefore = NormalizeBarrierSync(desc.srcStageMask, accessBefore, queueType);
        const D3D12_BARRIER_SYNC syncAfter = NormalizeBarrierSync(desc.dstStageMask, accessAfter, queueType);

        if (desc.texture)
        {
            const D3D12_BARRIER_LAYOUT layoutBefore = ConvertTextureLayout(desc.stateBefore, queueType);
            const D3D12_BARRIER_LAYOUT layoutAfter = ConvertTextureLayout(desc.stateAfter, queueType);

            barrier.kind = EnhancedBarrierKind::Texture;
            barrier.texture.SyncBefore = syncBefore;
            barrier.texture.SyncAfter = syncAfter;
            barrier.texture.AccessBefore = accessBefore;
            barrier.texture.AccessAfter = accessAfter;
            barrier.texture.LayoutBefore = layoutBefore;
            barrier.texture.LayoutAfter = layoutAfter;
            barrier.texture.pResource = resource;
            barrier.texture.Subresources = AllTextureSubresources();
            barrier.texture.Flags = GetTextureTransitionFlags(desc.stateBefore, layoutBefore, layoutAfter);
            return true;
        }

        barrier.kind = EnhancedBarrierKind::Buffer;
        barrier.buffer.SyncBefore = syncBefore;
        barrier.buffer.SyncAfter = syncAfter;
        barrier.buffer.AccessBefore = accessBefore;
        barrier.buffer.AccessAfter = accessAfter;
        barrier.buffer.pResource = resource;
        barrier.buffer.Offset = 0;
        barrier.buffer.Size = UINT64_MAX;
        return true;
    }

    if (desc.type == RHIBarrierDesc::Type::Aliasing)
    {
        if (TryBuildEnhancedTextureDeactivationBarrier(desc, queueType, barrier))
        {
            return true;
        }

        barrier.kind = EnhancedBarrierKind::Global;
        barrier.global.SyncBefore = D3D12_BARRIER_SYNC_ALL;
        barrier.global.SyncAfter = D3D12_BARRIER_SYNC_ALL;
        barrier.global.AccessBefore = D3D12_BARRIER_ACCESS_COMMON;
        barrier.global.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;
        return true;
    }

    if (desc.type == RHIBarrierDesc::Type::UAV)
    {
        D3D12_BARRIER_SYNC syncBefore =
            NormalizeBarrierSync(desc.srcStageMask, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, queueType);
        D3D12_BARRIER_SYNC syncAfter =
            NormalizeBarrierSync(desc.dstStageMask, D3D12_BARRIER_ACCESS_UNORDERED_ACCESS, queueType);

        if (desc.texture)
        {
            ID3D12Resource* resource = ResolveBarrierResource(desc);
            if (!resource)
            {
                WEST_LOG_WARNING(LogCategory::RHI, "ResourceBarrier: no valid texture for enhanced UAV barrier");
                return false;
            }

            barrier.kind = EnhancedBarrierKind::Texture;
            barrier.texture.SyncBefore = syncBefore;
            barrier.texture.SyncAfter = syncAfter;
            barrier.texture.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            barrier.texture.AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            barrier.texture.LayoutBefore = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
            barrier.texture.LayoutAfter = D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
            barrier.texture.pResource = resource;
            barrier.texture.Subresources = AllTextureSubresources();
            barrier.texture.Flags = D3D12_TEXTURE_BARRIER_FLAG_NONE;
            return true;
        }

        if (desc.buffer)
        {
            ID3D12Resource* resource = ResolveBarrierResource(desc);
            if (!resource)
            {
                WEST_LOG_WARNING(LogCategory::RHI, "ResourceBarrier: no valid buffer for enhanced UAV barrier");
                return false;
            }

            barrier.kind = EnhancedBarrierKind::Buffer;
            barrier.buffer.SyncBefore = syncBefore;
            barrier.buffer.SyncAfter = syncAfter;
            barrier.buffer.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            barrier.buffer.AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
            barrier.buffer.pResource = resource;
            barrier.buffer.Offset = 0;
            barrier.buffer.Size = UINT64_MAX;
            return true;
        }

        barrier.kind = EnhancedBarrierKind::Global;
        barrier.global.SyncBefore = syncBefore;
        barrier.global.SyncAfter = syncAfter;
        barrier.global.AccessBefore = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
        barrier.global.AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;
        return true;
    }

    return false;
}

[[nodiscard]] bool TryBuildD3D12Barrier(const RHIBarrierDesc& desc, D3D12_RESOURCE_BARRIER& barrier)
{
    barrier = {};

    if (desc.type == RHIBarrierDesc::Type::Transition)
    {
        ID3D12Resource* resource = ResolveBarrierResource(desc);
        if (!resource)
        {
            WEST_LOG_WARNING(LogCategory::RHI, "ResourceBarrier: no valid resource for transition");
            return false;
        }

        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = ConvertResourceState(desc.stateBefore);
        barrier.Transition.StateAfter = ConvertResourceState(desc.stateAfter);
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return true;
    }

    if (desc.type == RHIBarrierDesc::Type::Aliasing)
    {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        barrier.Aliasing.pResourceBefore =
            desc.aliasBefore ? static_cast<DX12Texture*>(desc.aliasBefore)->GetD3DResource() : nullptr;
        barrier.Aliasing.pResourceAfter =
            desc.aliasAfter ? static_cast<DX12Texture*>(desc.aliasAfter)->GetD3DResource() : nullptr;
        return true;
    }

    if (desc.type == RHIBarrierDesc::Type::UAV)
    {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = ResolveBarrierResource(desc);
        return true;
    }

    return false;
}

void LogD3D12InfoQueueMessages(ID3D12GraphicsCommandList* cmdList, const char* context)
{
    if (!cmdList)
    {
        return;
    }

    ComPtr<ID3D12Device> device;
    if (FAILED(cmdList->GetDevice(IID_PPV_ARGS(&device))))
    {
        return;
    }

    ComPtr<ID3D12InfoQueue> infoQueue;
    if (FAILED(device.As(&infoQueue)))
    {
        return;
    }

    const UINT64 messageCount = infoQueue->GetNumStoredMessages();
    const UINT64 firstMessage = messageCount > 16 ? messageCount - 16 : 0;

    for (UINT64 i = firstMessage; i < messageCount; ++i)
    {
        SIZE_T messageLength = 0;
        if (FAILED(infoQueue->GetMessage(i, nullptr, &messageLength)) || messageLength == 0)
        {
            continue;
        }

        std::vector<uint8_t> messageStorage(messageLength);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(messageStorage.data());
        if (SUCCEEDED(infoQueue->GetMessage(i, message, &messageLength)))
        {
            WEST_LOG_ERROR(LogCategory::RHI, "D3D12 {}: {}", context, message->pDescription);
        }
    }
}

} // namespace

void DX12CommandList::Initialize(ID3D12Device* device, RHIQueueType type, ID3D12DescriptorHeap* resourceHeap,
                                 ID3D12DescriptorHeap* samplerHeap)
{
    m_queueType = type;
    m_resourceDescriptorHeap = resourceHeap;
    m_samplerDescriptorHeap = samplerHeap;

    D3D12_COMMAND_LIST_TYPE d3dType = D3D12_COMMAND_LIST_TYPE_DIRECT;
    switch (type)
    {
    case RHIQueueType::Graphics:
        d3dType = D3D12_COMMAND_LIST_TYPE_DIRECT;
        break;
    case RHIQueueType::Compute:
        d3dType = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        break;
    case RHIQueueType::Copy:
        d3dType = D3D12_COMMAND_LIST_TYPE_COPY;
        break;
    }

    WEST_HR_CHECK(device->CreateCommandAllocator(d3dType, IID_PPV_ARGS(&m_allocator)));

    // CreateCommandList creates in open state — close immediately so Reset()/Begin() can reopen
    // We use the base ID3D12Device interface (ID3D12Device::CreateCommandList)
    ComPtr<ID3D12GraphicsCommandList> rawCmdList;
    WEST_HR_CHECK(device->CreateCommandList(0, d3dType, m_allocator.Get(), nullptr, IID_PPV_ARGS(&rawCmdList)));
    WEST_HR_CHECK(rawCmdList.As(&m_cmdList));
    D3D12_FEATURE_DATA_D3D12_OPTIONS12 options12{};
    const HRESULT optionsHr = device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12, &options12, sizeof(options12));
    const HRESULT cmdList7Hr = m_cmdList.As(&m_cmdList7);
    m_enhancedBarriersEnabled = type != RHIQueueType::Copy &&
                                SUCCEEDED(optionsHr) &&
                                options12.EnhancedBarriersSupported &&
                                SUCCEEDED(cmdList7Hr);
    WEST_HR_CHECK(m_cmdList->Close()); // Close so Begin() can Reset+Reopen

    D3D12_INDIRECT_ARGUMENT_DESC indirectArgument{};
    indirectArgument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
    signatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    signatureDesc.NumArgumentDescs = 1;
    signatureDesc.pArgumentDescs = &indirectArgument;
    WEST_HR_CHECK(device->CreateCommandSignature(&signatureDesc, nullptr,
                                                 IID_PPV_ARGS(&m_drawIndexedIndirectSignature)));

    WEST_LOG_VERBOSE(LogCategory::RHI, "DX12 CommandList created. Enhanced barriers: {}",
                     m_enhancedBarriersEnabled ? "enabled" : "disabled");
}

// ── Recording Lifecycle ───────────────────────────────────────────────────

void DX12CommandList::Begin()
{
    // Reset allocator first (must wait for GPU to finish using it)
    WEST_HR_CHECK(m_allocator->Reset());
    // Reset command list with the allocator
    WEST_HR_CHECK(m_cmdList->Reset(m_allocator.Get(), nullptr));

    ID3D12DescriptorHeap* heaps[] = {m_resourceDescriptorHeap, m_samplerDescriptorHeap};
    uint32_t heapCount = 0;
    for (ID3D12DescriptorHeap* heap : heaps)
    {
        if (heap)
        {
            heaps[heapCount++] = heap;
        }
    }
    if (heapCount > 0)
    {
        m_cmdList->SetDescriptorHeaps(heapCount, heaps);
    }
}

void DX12CommandList::End()
{
    const HRESULT hr = m_cmdList->Close();
    if (FAILED(hr))
    {
        LogD3D12InfoQueueMessages(m_cmdList.Get(), "command list close");
    }
    WEST_HR_CHECK(hr);
}

void DX12CommandList::Reset()
{
    // Begin() handles full reset. This is a no-op for DX12.
}

// ── Barrier ───────────────────────────────────────────────────────────────

void DX12CommandList::ResourceBarrier(const RHIBarrierDesc& desc)
{
    ResourceBarriers(std::span<const RHIBarrierDesc>(&desc, 1));
}

void DX12CommandList::ResourceBarriers(std::span<const RHIBarrierDesc> descs)
{
    if (descs.empty())
    {
        return;
    }

    if (m_enhancedBarriersEnabled && m_cmdList7)
    {
        std::vector<EnhancedBarrier> enhancedBarriers;
        enhancedBarriers.reserve(descs.size());
        for (const RHIBarrierDesc& desc : descs)
        {
            EnhancedBarrier barrier{};
            if (TryBuildEnhancedD3D12Barrier(desc, m_queueType, barrier))
            {
                enhancedBarriers.push_back(barrier);
            }
        }

        for (const EnhancedBarrier& barrier : enhancedBarriers)
        {
            D3D12_BARRIER_GROUP group{};
            group.NumBarriers = 1;
            switch (barrier.kind)
            {
            case EnhancedBarrierKind::Global:
                group.Type = D3D12_BARRIER_TYPE_GLOBAL;
                group.pGlobalBarriers = &barrier.global;
                break;
            case EnhancedBarrierKind::Texture:
                group.Type = D3D12_BARRIER_TYPE_TEXTURE;
                group.pTextureBarriers = &barrier.texture;
                break;
            case EnhancedBarrierKind::Buffer:
                group.Type = D3D12_BARRIER_TYPE_BUFFER;
                group.pBufferBarriers = &barrier.buffer;
                break;
            }

            m_cmdList7->Barrier(1, &group);
        }
        return;
    }

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(descs.size());
    for (const RHIBarrierDesc& desc : descs)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        if (TryBuildD3D12Barrier(desc, barrier))
        {
            barriers.push_back(barrier);
        }
    }

    if (!barriers.empty())
    {
        m_cmdList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    }
}

// ── Viewport & Scissor ────────────────────────────────────────────────────

void DX12CommandList::SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth)
{
    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = x;
    viewport.TopLeftY = y;
    viewport.Width = w;
    viewport.Height = h;
    viewport.MinDepth = minDepth;
    viewport.MaxDepth = maxDepth;
    m_cmdList->RSSetViewports(1, &viewport);
}

void DX12CommandList::SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    D3D12_RECT rect{};
    rect.left = x;
    rect.top = y;
    rect.right = x + static_cast<LONG>(w);
    rect.bottom = y + static_cast<LONG>(h);
    m_cmdList->RSSetScissorRects(1, &rect);
}

// ── Render Pass (Phase 1: simplified — used for ClearColor path) ──────────

void DX12CommandList::BeginRenderPass(const RHIRenderPassDesc& desc)
{
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
    rtvHandles.reserve(desc.colorAttachments.size());

    for (const RHIColorAttachment& colorAttach : desc.colorAttachments)
    {
        if (!colorAttach.texture)
        {
            continue;
        }

        auto* dx12Tex = static_cast<DX12Texture*>(colorAttach.texture);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = dx12Tex->GetRTV();
        rtvHandles.push_back(rtv);

        if (colorAttach.loadOp == RHILoadOp::Clear)
        {
            m_cmdList->ClearRenderTargetView(rtv, colorAttach.clearColor, 0, nullptr);
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE* dsvPtr = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
    if (desc.depthAttachment.texture)
    {
        auto* dx12Depth = static_cast<DX12Texture*>(desc.depthAttachment.texture);
        dsvHandle = dx12Depth->GetDSV();
        dsvPtr = &dsvHandle;

        if (desc.depthAttachment.loadOp == RHILoadOp::Clear)
        {
            D3D12_CLEAR_FLAGS clearFlags = D3D12_CLEAR_FLAG_DEPTH;
            if (IsStencilFormat(dx12Depth->GetDesc().format))
            {
                clearFlags |= D3D12_CLEAR_FLAG_STENCIL;
            }

            m_cmdList->ClearDepthStencilView(dsvHandle, clearFlags, desc.depthAttachment.clearDepth,
                                             desc.depthAttachment.clearStencil, 0, nullptr);
        }
    }

    if (!rtvHandles.empty() || dsvPtr)
    {
        m_cmdList->OMSetRenderTargets(static_cast<UINT>(rtvHandles.size()),
                                      rtvHandles.empty() ? nullptr : rtvHandles.data(),
                                      FALSE, dsvPtr);
    }
}

void DX12CommandList::EndRenderPass()
{
    // Phase 1: no-op. Phase 5+: ID3D12GraphicsCommandList4::EndRenderPass
}

// ── Stub implementations (Phase 2+) ──────────────────────────────────────

void DX12CommandList::SetPipeline(IRHIPipeline* pipeline)
{
    auto* dx12Pipeline = static_cast<DX12Pipeline*>(pipeline);
    WEST_CHECK(dx12Pipeline != nullptr, "DX12CommandList::SetPipeline received a null pipeline");
    m_currentPipelineType = dx12Pipeline->GetType();
    m_cmdList->SetPipelineState(dx12Pipeline->GetPipelineState());
    if (m_currentPipelineType == RHIPipelineType::Compute)
    {
        m_cmdList->SetComputeRootSignature(dx12Pipeline->GetRootSignature());
    }
    else
    {
        m_cmdList->SetGraphicsRootSignature(dx12Pipeline->GetRootSignature());
        m_cmdList->IASetPrimitiveTopology(dx12Pipeline->GetPrimitiveTopology());
    }
}

void DX12CommandList::SetPushConstants(const void* data, uint32_t sizeBytes)
{
    WEST_CHECK(data != nullptr, "DX12CommandList::SetPushConstants received null data");
    WEST_CHECK(sizeBytes > 0 && (sizeBytes % sizeof(uint32_t)) == 0,
               "DX12CommandList::SetPushConstants size must be non-zero and 4-byte aligned");
    WEST_CHECK(sizeBytes <= kMaxPushConstantSizeBytes,
               "DX12CommandList::SetPushConstants size {} exceeds limit {}", sizeBytes, kMaxPushConstantSizeBytes);
    if (m_currentPipelineType == RHIPipelineType::Compute)
    {
        m_cmdList->SetComputeRoot32BitConstants(0, sizeBytes / sizeof(uint32_t), data, 0);
    }
    else
    {
        m_cmdList->SetGraphicsRoot32BitConstants(0, sizeBytes / sizeof(uint32_t), data, 0);
    }
}

void DX12CommandList::SetVertexBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset)
{
    auto* dx12Buffer = static_cast<DX12Buffer*>(buffer);
    WEST_CHECK(dx12Buffer != nullptr, "DX12CommandList::SetVertexBuffer received a null buffer");
    WEST_CHECK(offset <= dx12Buffer->GetDesc().sizeBytes,
               "DX12CommandList::SetVertexBuffer offset {} exceeds buffer size {}", offset,
               dx12Buffer->GetDesc().sizeBytes);

    D3D12_VERTEX_BUFFER_VIEW vbView{};
    vbView.BufferLocation = dx12Buffer->GetGPUVirtualAddress() + offset;
    vbView.SizeInBytes = static_cast<UINT>(dx12Buffer->GetDesc().sizeBytes - offset);
    vbView.StrideInBytes = dx12Buffer->GetDesc().structureByteStride;
    m_cmdList->IASetVertexBuffers(slot, 1, &vbView);
}

void DX12CommandList::SetIndexBuffer(IRHIBuffer* buffer, RHIFormat format, uint64_t offset)
{
    auto* dx12Buffer = static_cast<DX12Buffer*>(buffer);
    WEST_CHECK(dx12Buffer != nullptr, "DX12CommandList::SetIndexBuffer received a null buffer");
    WEST_CHECK(format == RHIFormat::R32_UINT,
               "DX12CommandList::SetIndexBuffer requires R32_UINT format");
    WEST_CHECK(offset <= dx12Buffer->GetDesc().sizeBytes,
               "DX12CommandList::SetIndexBuffer offset {} exceeds buffer size {}", offset,
               dx12Buffer->GetDesc().sizeBytes);

    D3D12_INDEX_BUFFER_VIEW ibView{};
    ibView.BufferLocation = dx12Buffer->GetGPUVirtualAddress() + offset;
    ibView.SizeInBytes = static_cast<UINT>(dx12Buffer->GetDesc().sizeBytes - offset);
    ibView.Format = static_cast<DXGI_FORMAT>(ToDXGIFormat(format));
    m_cmdList->IASetIndexBuffer(&ibView);
}

void DX12CommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                           uint32_t firstInstance)
{
    m_cmdList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void DX12CommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                                  int32_t vertexOffset, uint32_t firstInstance)
{
    m_cmdList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void DX12CommandList::DrawIndexedIndirectCount(IRHIBuffer* argsBuffer, uint64_t argsOffset,
                                               IRHIBuffer* countBuffer, uint64_t countOffset,
                                               uint32_t maxDrawCount, uint32_t stride)
{
    WEST_CHECK(argsBuffer != nullptr, "DX12CommandList::DrawIndexedIndirectCount received null args buffer");
    WEST_CHECK(countBuffer != nullptr, "DX12CommandList::DrawIndexedIndirectCount received null count buffer");
    WEST_CHECK(m_drawIndexedIndirectSignature != nullptr, "DX12 indirect command signature is not initialized");
    WEST_CHECK(stride == sizeof(D3D12_DRAW_INDEXED_ARGUMENTS),
               "DX12CommandList::DrawIndexedIndirectCount stride {} is unsupported", stride);

    auto* dx12ArgsBuffer = static_cast<DX12Buffer*>(argsBuffer);
    auto* dx12CountBuffer = static_cast<DX12Buffer*>(countBuffer);
    WEST_CHECK(dx12ArgsBuffer != nullptr && dx12CountBuffer != nullptr,
               "DX12CommandList::DrawIndexedIndirectCount received a non-DX12 buffer");
    WEST_CHECK(argsOffset + static_cast<uint64_t>(maxDrawCount) * stride <= dx12ArgsBuffer->GetDesc().sizeBytes,
               "DX12 indirect args range exceeds buffer size");
    WEST_CHECK(countOffset + sizeof(uint32_t) <= dx12CountBuffer->GetDesc().sizeBytes,
               "DX12 indirect count range exceeds buffer size");

    m_cmdList->ExecuteIndirect(m_drawIndexedIndirectSignature.Get(), maxDrawCount,
                               dx12ArgsBuffer->GetD3DResource(), argsOffset,
                               dx12CountBuffer->GetD3DResource(), countOffset);
}

void DX12CommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    m_cmdList->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void DX12CommandList::CopyBuffer(IRHIBuffer* src, uint64_t srcOffset, IRHIBuffer* dst,
                                 uint64_t dstOffset, uint64_t size)
{
    auto* dx12Src = static_cast<DX12Buffer*>(src);
    auto* dx12Dst = static_cast<DX12Buffer*>(dst);
    WEST_CHECK(dx12Src != nullptr && dx12Dst != nullptr, "DX12CommandList::CopyBuffer received a null buffer");
    WEST_CHECK(srcOffset <= dx12Src->GetDesc().sizeBytes && size <= dx12Src->GetDesc().sizeBytes - srcOffset,
               "DX12CommandList::CopyBuffer source range exceeds buffer size");
    WEST_CHECK(dstOffset <= dx12Dst->GetDesc().sizeBytes && size <= dx12Dst->GetDesc().sizeBytes - dstOffset,
               "DX12CommandList::CopyBuffer destination range exceeds buffer size");

    m_cmdList->CopyBufferRegion(dx12Dst->GetD3DResource(), dstOffset,
                                dx12Src->GetD3DResource(), srcOffset, size);
}

void DX12CommandList::CopyBufferToTexture(IRHIBuffer* src, IRHITexture* dst, const RHICopyRegion& region)
{
    auto* dx12Src = static_cast<DX12Buffer*>(src);
    auto* dx12Dst = static_cast<DX12Texture*>(dst);
    WEST_CHECK(dx12Src != nullptr && dx12Dst != nullptr,
               "DX12CommandList::CopyBufferToTexture received a null resource");

    const RHITextureDesc& desc = dx12Dst->GetDesc();
    WEST_CHECK(region.texWidth > 0 && region.texHeight > 0 && region.texDepth > 0,
               "DX12CommandList::CopyBufferToTexture requires a non-empty copy extent");
    WEST_CHECK(region.mipLevel < desc.mipLevels && region.arrayLayer < desc.arrayLayers,
               "DX12CommandList::CopyBufferToTexture subresource is out of range");
    WEST_CHECK(region.texOffsetX + region.texWidth <= desc.width &&
                   region.texOffsetY + region.texHeight <= desc.height &&
                   region.texOffsetZ + region.texDepth <= desc.depth,
               "DX12CommandList::CopyBufferToTexture destination region exceeds texture bounds");

    const uint32_t bytesPerBlock = GetFormatByteSize(desc.format);
    WEST_ASSERT(bytesPerBlock > 0);

    D3D12_TEXTURE_COPY_LOCATION srcLocation{};
    srcLocation.pResource = dx12Src->GetD3DResource();
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint.Offset = region.bufferOffset;
    srcLocation.PlacedFootprint.Footprint.Format = static_cast<DXGI_FORMAT>(ToDXGIFormat(desc.format));
    srcLocation.PlacedFootprint.Footprint.Width = region.texWidth;
    srcLocation.PlacedFootprint.Footprint.Height =
        region.bufferImageHeight == 0 ? region.texHeight : region.bufferImageHeight;
    srcLocation.PlacedFootprint.Footprint.Depth = region.texDepth;

    const uint32_t blockWidth = GetFormatBlockWidth(desc.format);
    const uint32_t blockHeight = GetFormatBlockHeight(desc.format);
    const uint32_t rowLengthTexels = region.bufferRowLength == 0 ? region.texWidth : region.bufferRowLength;
    const uint32_t rowBlocks = (rowLengthTexels + blockWidth - 1u) / blockWidth;
    const uint32_t rowPitch = rowBlocks * bytesPerBlock;
    const uint32_t alignedRowPitch =
        (rowPitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);
    const uint32_t copyHeightBlocks =
        (srcLocation.PlacedFootprint.Footprint.Height + blockHeight - 1u) / blockHeight;
    const uint64_t slicePitch = static_cast<uint64_t>(alignedRowPitch) * copyHeightBlocks;
    const uint64_t requiredBytes =
        region.bufferOffset + (static_cast<uint64_t>(region.texDepth) - 1u) * slicePitch +
        (static_cast<uint64_t>(copyHeightBlocks) - 1u) * alignedRowPitch + rowPitch;
    WEST_CHECK(requiredBytes <= dx12Src->GetDesc().sizeBytes,
               "DX12CommandList::CopyBufferToTexture source range exceeds buffer size");
    srcLocation.PlacedFootprint.Footprint.RowPitch = alignedRowPitch;

    D3D12_TEXTURE_COPY_LOCATION dstLocation{};
    dstLocation.pResource = dx12Dst->GetD3DResource();
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = region.mipLevel + region.arrayLayer * desc.mipLevels;

    D3D12_BOX srcBox{};
    srcBox.left = 0;
    srcBox.top = 0;
    srcBox.front = 0;
    srcBox.right = region.texWidth;
    srcBox.bottom = region.texHeight;
    srcBox.back = region.texDepth;

    m_cmdList->CopyTextureRegion(&dstLocation, region.texOffsetX, region.texOffsetY, region.texOffsetZ,
                                 &srcLocation, &srcBox);
}

void DX12CommandList::ResetTimestampQueries(IRHITimestampQueryPool* /*queryPool*/, uint32_t /*firstQuery*/,
                                            uint32_t /*queryCount*/)
{
    // D3D12 timestamp query slots are overwritten by EndQuery; no explicit reset command exists.
}

void DX12CommandList::WriteTimestamp(IRHITimestampQueryPool* queryPool, uint32_t index)
{
    auto* dx12QueryPool = static_cast<DX12TimestampQueryPool*>(queryPool);
    WEST_CHECK(dx12QueryPool != nullptr, "DX12CommandList::WriteTimestamp received a null query pool");
    WEST_CHECK(index < dx12QueryPool->GetDesc().queryCount,
               "DX12CommandList::WriteTimestamp query index {} is out of range", index);

    m_cmdList->EndQuery(dx12QueryPool->GetD3DQueryHeap(), D3D12_QUERY_TYPE_TIMESTAMP, index);
}

void DX12CommandList::ResolveTimestampQueries(IRHITimestampQueryPool* queryPool, uint32_t firstQuery,
                                              uint32_t queryCount)
{
    if (queryCount == 0)
    {
        return;
    }

    auto* dx12QueryPool = static_cast<DX12TimestampQueryPool*>(queryPool);
    WEST_CHECK(dx12QueryPool != nullptr, "DX12CommandList::ResolveTimestampQueries received a null query pool");
    WEST_CHECK(firstQuery <= dx12QueryPool->GetDesc().queryCount &&
                   queryCount <= dx12QueryPool->GetDesc().queryCount - firstQuery,
               "DX12CommandList::ResolveTimestampQueries query range is out of bounds");

    m_cmdList->ResolveQueryData(dx12QueryPool->GetD3DQueryHeap(), D3D12_QUERY_TYPE_TIMESTAMP,
                                firstQuery, queryCount, dx12QueryPool->GetReadbackResource(),
                                static_cast<UINT64>(firstQuery) * sizeof(uint64_t));
}

} // namespace west::rhi
