// =============================================================================
// WestEngine - RHI Interface
// Abstract command list — recording draw/dispatch/barrier/copy commands
// =============================================================================
#pragma once

#include "rhi/interface/RHIDescriptors.h"
#include "rhi/interface/RHIEnums.h"

#include <cstdint>

namespace west::rhi
{

class IRHIBuffer;
class IRHITexture;
class IRHIPipeline;

class IRHICommandList
{
public:
    virtual ~IRHICommandList() = default;

    // ── Recording Lifecycle ───────────────────────────────────────────
    virtual void Begin() = 0;
    virtual void End() = 0;
    virtual void Reset() = 0;

    // ── Pipeline Binding ──────────────────────────────────────────────
    virtual void SetPipeline(IRHIPipeline* pipeline) = 0;

    // ── Bindless Push Constants ───────────────────────────────────────
    virtual void SetPushConstants(const void* data, uint32_t sizeBytes) = 0;

    // ── Vertex / Index Binding ────────────────────────────────────────
    virtual void SetVertexBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset = 0) = 0;
    virtual void SetIndexBuffer(IRHIBuffer* buffer, RHIFormat format, uint64_t offset = 0) = 0;

    // ── Viewport & Scissor ────────────────────────────────────────────
    virtual void SetViewport(float x, float y, float w, float h, float minDepth = 0.0f, float maxDepth = 1.0f) = 0;
    virtual void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) = 0;

    // ── Draw ──────────────────────────────────────────────────────────
    virtual void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
                      uint32_t firstInstance = 0) = 0;
    virtual void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0,
                             int32_t vertexOffset = 0, uint32_t firstInstance = 0) = 0;

    // ── Indirect Draw (GPU-Driven) ────────────────────────────────────
    virtual void DrawIndexedIndirectCount(IRHIBuffer* argsBuffer, uint64_t argsOffset, IRHIBuffer* countBuffer,
                                          uint64_t countOffset, uint32_t maxDrawCount, uint32_t stride) = 0;

    // ── Compute Dispatch ──────────────────────────────────────────────
    virtual void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) = 0;

    // ── Render Pass ───────────────────────────────────────────────────
    virtual void BeginRenderPass(const RHIRenderPassDesc& desc) = 0;
    virtual void EndRenderPass() = 0;

    // ── Barrier ───────────────────────────────────────────────────────
    virtual void ResourceBarrier(const RHIBarrierDesc& desc) = 0;
    virtual void ResourceBarriers(std::span<const RHIBarrierDesc> descs)
    {
        for (const RHIBarrierDesc& desc : descs)
        {
            ResourceBarrier(desc);
        }
    }

    // ── Copy ──────────────────────────────────────────────────────────
    virtual void CopyBuffer(IRHIBuffer* src, uint64_t srcOffset, IRHIBuffer* dst, uint64_t dstOffset,
                            uint64_t size) = 0;
    virtual void CopyBufferToTexture(IRHIBuffer* src, IRHITexture* dst, const RHICopyRegion& region) = 0;

    // ── Timestamp (Profiling) ─────────────────────────────────────────
    virtual void WriteTimestamp(IRHIBuffer* queryBuffer, uint32_t index) = 0;

    // ── Queue Type Query ──────────────────────────────────────────────
    virtual RHIQueueType GetQueueType() const = 0;
};

} // namespace west::rhi
