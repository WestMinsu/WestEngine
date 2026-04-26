// =============================================================================
// WestEngine - RHI DX12
// DX12 command list — command recording with allocator management
// =============================================================================
#pragma once

#include "rhi/dx12/DX12Helpers.h"
#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/IRHIPipeline.h"

namespace west::rhi
{

class DX12CommandList final : public IRHICommandList
{
public:
    DX12CommandList() = default;
    ~DX12CommandList() override = default;
    DX12CommandList(const DX12CommandList&) = delete;
    DX12CommandList& operator=(const DX12CommandList&) = delete;
    DX12CommandList(DX12CommandList&&) = delete;
    DX12CommandList& operator=(DX12CommandList&&) = delete;

    /// Initialize command allocator + command list.
    void Initialize(ID3D12Device* device, RHIQueueType type, ID3D12DescriptorHeap* resourceHeap,
                    ID3D12DescriptorHeap* samplerHeap);

    // ── IRHICommandList interface ─────────────────────────────────────
    void Begin() override;
    void End() override;
    void Reset() override;

    void SetPipeline(IRHIPipeline* pipeline) override;
    void SetPushConstants(const void* data, uint32_t sizeBytes) override;
    void SetVertexBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset = 0) override;
    void SetIndexBuffer(IRHIBuffer* buffer, RHIFormat format, uint64_t offset = 0) override;
    void SetViewport(float x, float y, float w, float h, float minDepth = 0.0f, float maxDepth = 1.0f) override;
    void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) override;

    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
              uint32_t firstInstance = 0) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0) override;
    void DrawIndexedIndirectCount(IRHIBuffer* argsBuffer, uint64_t argsOffset, IRHIBuffer* countBuffer,
                                  uint64_t countOffset, uint32_t maxDrawCount, uint32_t stride) override;
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

    void BeginRenderPass(const RHIRenderPassDesc& desc) override;
    void EndRenderPass() override;
    void ResourceBarrier(const RHIBarrierDesc& desc) override;
    void ResourceBarriers(std::span<const RHIBarrierDesc> descs) override;

    void CopyBuffer(IRHIBuffer* src, uint64_t srcOffset, IRHIBuffer* dst, uint64_t dstOffset, uint64_t size) override;
    void CopyBufferToTexture(IRHIBuffer* src, IRHITexture* dst, const RHICopyRegion& region) override;
    void ResetTimestampQueries(IRHITimestampQueryPool* queryPool, uint32_t firstQuery,
                               uint32_t queryCount) override;
    void WriteTimestamp(IRHITimestampQueryPool* queryPool, uint32_t index) override;
    void ResolveTimestampQueries(IRHITimestampQueryPool* queryPool, uint32_t firstQuery,
                                 uint32_t queryCount) override;

    RHIQueueType GetQueueType() const override
    {
        return m_queueType;
    }

    // ── Internal ──────────────────────────────────────────────────────
    ID3D12GraphicsCommandList6* GetD3DCommandList() const
    {
        return m_cmdList.Get();
    }

private:
    ComPtr<ID3D12CommandAllocator> m_allocator;
    ComPtr<ID3D12GraphicsCommandList6> m_cmdList;
    ComPtr<ID3D12GraphicsCommandList7> m_cmdList7;
    ComPtr<ID3D12CommandSignature> m_drawIndexedIndirectSignature;
    ID3D12DescriptorHeap* m_resourceDescriptorHeap = nullptr;
    ID3D12DescriptorHeap* m_samplerDescriptorHeap = nullptr;
    RHIPipelineType m_currentPipelineType = RHIPipelineType::Graphics;
    RHIQueueType m_queueType = RHIQueueType::Graphics;
    bool m_enhancedBarriersEnabled = false;
};

} // namespace west::rhi
