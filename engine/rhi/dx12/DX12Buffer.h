// =============================================================================
// WestEngine - RHI DX12
// DX12 buffer — D3D12MA Placed Resource backed buffer
// =============================================================================
#pragma once

#include "rhi/dx12/DX12Helpers.h"
#include "rhi/interface/IRHIBuffer.h"

// Forward declare D3D12MA types to avoid dxguids header conflict
namespace D3D12MA { class Allocation; }

namespace west::rhi
{

class DX12MemoryAllocator;
class DX12Device;

class DX12Buffer final : public IRHIBuffer
{
public:
    DX12Buffer() = default;
    ~DX12Buffer() override;
    DX12Buffer(const DX12Buffer&) = delete;
    DX12Buffer& operator=(const DX12Buffer&) = delete;
    DX12Buffer(DX12Buffer&&) = delete;
    DX12Buffer& operator=(DX12Buffer&&) = delete;

    /// Allocate a buffer via D3D12MA.
    void Initialize(DX12Device* device, const RHIBufferDesc& desc);

    // ── IRHIBuffer interface ──────────────────────────────────────────
    const RHIBufferDesc& GetDesc() const override { return m_desc; }
    void* Map() override;
    void Unmap() override;
    BindlessIndex GetBindlessIndex() const override { return m_bindlessIndex; }

    // ── Internal ──────────────────────────────────────────────────────
    ID3D12Resource* GetD3DResource() const { return m_resource; }
    D3D12_GPU_VIRTUAL_ADDRESS GetGPUVirtualAddress() const;
    void SetBindlessIndex(BindlessIndex index) { m_bindlessIndex = index; }

private:
    D3D12MA::Allocation* m_allocation = nullptr;
    ID3D12Resource* m_resource = nullptr; // Owned by the allocation
    RHIBufferDesc m_desc{};
    BindlessIndex m_bindlessIndex = kInvalidBindlessIndex;
    void* m_mappedPtr = nullptr;
    DX12Device* m_device = nullptr;
};

} // namespace west::rhi
