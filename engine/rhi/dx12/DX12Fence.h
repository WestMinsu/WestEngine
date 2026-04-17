// =============================================================================
// WestEngine - RHI DX12
// DX12 fence (Timeline Semaphore equivalent) — CPU-GPU synchronization
// =============================================================================
#pragma once

#include "rhi/dx12/DX12Helpers.h"
#include "rhi/interface/IRHIFence.h"

#include <atomic>

namespace west::rhi
{

class DX12Fence final : public IRHIFence
{
public:
    DX12Fence() = default;
    ~DX12Fence() override;

    /// Initialize the DX12 fence.
    void Initialize(ID3D12Device* device, uint64_t initialValue = 0);

    // ── IRHIFence interface ───────────────────────────────────────────
    uint64_t GetCompletedValue() const override;
    void Wait(uint64_t value, uint64_t timeoutNs = UINT64_MAX) override;
    uint64_t AdvanceValue() override;

    // ── Internal ──────────────────────────────────────────────────────
    ID3D12Fence* GetD3DFence() const
    {
        return m_fence.Get();
    }

private:
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    std::atomic<uint64_t> m_nextValue{0};
};

} // namespace west::rhi
