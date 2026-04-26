// =============================================================================
// WestEngine - RHI DX12
// DX12 pipeline — minimal PSO for Phase 2 triangle rendering
// =============================================================================
#pragma once

#include "rhi/dx12/DX12Helpers.h"
#include "rhi/interface/IRHIPipeline.h"
#include "rhi/interface/RHIDescriptors.h"

namespace west::rhi
{

class DX12Device;

class DX12Pipeline final : public IRHIPipeline
{
public:
    DX12Pipeline() = default;
    ~DX12Pipeline() override;
    DX12Pipeline(const DX12Pipeline&) = delete;
    DX12Pipeline& operator=(const DX12Pipeline&) = delete;
    DX12Pipeline(DX12Pipeline&&) = delete;
    DX12Pipeline& operator=(DX12Pipeline&&) = delete;

    /// Create a graphics PSO with the given desc.
    void Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature, const RHIGraphicsPipelineDesc& desc);
    void Initialize(ID3D12Device* device, ID3D12RootSignature* rootSignature, const RHIComputePipelineDesc& desc);

    // ── IRHIPipeline interface ────────────────────────────────────────
    RHIPipelineType GetType() const override { return m_type; }
    uint64_t GetPSOHash() const override { return m_psoHash; }

    // ── Internal ──────────────────────────────────────────────────────
    void SetOwnerDevice(DX12Device* device) { m_ownerDevice = device; }
    ID3D12PipelineState* GetPipelineState() const { return m_pso.Get(); }
    ID3D12RootSignature* GetRootSignature() const { return m_rootSignature; }
    D3D_PRIMITIVE_TOPOLOGY GetPrimitiveTopology() const { return m_primitiveTopology; }

private:
    DX12Device* m_ownerDevice = nullptr;
    ComPtr<ID3D12PipelineState> m_pso;
    ID3D12RootSignature* m_rootSignature = nullptr;
    D3D_PRIMITIVE_TOPOLOGY m_primitiveTopology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    RHIPipelineType m_type = RHIPipelineType::Graphics;
    uint64_t m_psoHash = 0;
};

} // namespace west::rhi
