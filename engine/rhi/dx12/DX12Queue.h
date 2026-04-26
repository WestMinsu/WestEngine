// =============================================================================
// WestEngine - RHI DX12
// DX12 command queue wrapper
// =============================================================================
#pragma once

#include "rhi/dx12/DX12Helpers.h"
#include "rhi/interface/IRHIQueue.h"

namespace west::rhi
{

class DX12Queue final : public IRHIQueue
{
public:
    DX12Queue() = default;
    ~DX12Queue() override = default;
    DX12Queue(const DX12Queue&) = delete;
    DX12Queue& operator=(const DX12Queue&) = delete;
    DX12Queue(DX12Queue&&) = delete;
    DX12Queue& operator=(DX12Queue&&) = delete;

    /// Initialize the DX12 command queue.
    void Initialize(ID3D12Device* device, RHIQueueType type);

    // ── IRHIQueue interface ───────────────────────────────────────────
    void Submit(const RHISubmitInfo& info) override;
    RHIQueueType GetType() const override
    {
        return m_type;
    }

    // ── Internal ──────────────────────────────────────────────────────
    ID3D12CommandQueue* GetD3DQueue() const
    {
        return m_queue.Get();
    }

private:
    ComPtr<ID3D12CommandQueue> m_queue;
    RHIQueueType m_type = RHIQueueType::Graphics;
};

} // namespace west::rhi
