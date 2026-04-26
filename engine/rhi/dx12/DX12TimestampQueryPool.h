// =============================================================================
// WestEngine - RHI DX12
// Timestamp query pool with CPU readback storage
// =============================================================================
#pragma once

#include "rhi/dx12/DX12Helpers.h"
#include "rhi/interface/IRHITimestampQueryPool.h"

namespace west::rhi
{

class DX12Device;

class DX12TimestampQueryPool final : public IRHITimestampQueryPool
{
public:
    DX12TimestampQueryPool() = default;
    ~DX12TimestampQueryPool() override;
    DX12TimestampQueryPool(const DX12TimestampQueryPool&) = delete;
    DX12TimestampQueryPool& operator=(const DX12TimestampQueryPool&) = delete;
    DX12TimestampQueryPool(DX12TimestampQueryPool&&) = delete;
    DX12TimestampQueryPool& operator=(DX12TimestampQueryPool&&) = delete;

    void Initialize(DX12Device* device, const RHITimestampQueryPoolDesc& desc);

    const RHITimestampQueryPoolDesc& GetDesc() const override
    {
        return m_desc;
    }

    float GetTimestampPeriodNanoseconds() const override
    {
        return m_timestampPeriodNanoseconds;
    }

    bool ReadTimestamps(uint32_t firstQuery, uint32_t queryCount,
                        std::span<uint64_t> timestamps) override;

    ID3D12QueryHeap* GetD3DQueryHeap() const
    {
        return m_queryHeap.Get();
    }

    ID3D12Resource* GetReadbackResource() const
    {
        return m_readbackBuffer.Get();
    }

private:
    DX12Device* m_ownerDevice = nullptr;
    RHITimestampQueryPoolDesc m_desc{};
    ComPtr<ID3D12QueryHeap> m_queryHeap;
    ComPtr<ID3D12Resource> m_readbackBuffer;
    float m_timestampPeriodNanoseconds = 0.0f;
};

} // namespace west::rhi
