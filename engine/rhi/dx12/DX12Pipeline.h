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

class DX12Pipeline final : public IRHIPipeline
{
public:
    DX12Pipeline() = default;
    ~DX12Pipeline() override = default;

    /// Create a graphics PSO with the given desc.
    void Initialize(ID3D12Device* device, const RHIGraphicsPipelineDesc& desc);

    // ── IRHIPipeline interface ────────────────────────────────────────
    uint64_t GetPSOHash() const override { return m_psoHash; }

    // ── Internal ──────────────────────────────────────────────────────
    ID3D12PipelineState* GetPipelineState() const { return m_pso.Get(); }
    ID3D12RootSignature* GetRootSignature() const { return m_rootSignature.Get(); }

private:
    void CreateRootSignature(ID3D12Device* device);

    ComPtr<ID3D12PipelineState> m_pso;
    ComPtr<ID3D12RootSignature> m_rootSignature;
    uint64_t m_psoHash = 0;
};

} // namespace west::rhi
