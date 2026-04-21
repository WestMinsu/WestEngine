// =============================================================================
// WestEngine - RHI DX12
// DX12 texture — D3D12MA backed image or non-owning swapchain back buffer
// =============================================================================
#pragma once

#include "rhi/dx12/DX12Helpers.h"
#include "rhi/interface/IRHITexture.h"

namespace D3D12MA { class Allocation; }

namespace west::rhi
{

class DX12Device;

class DX12Texture final : public IRHITexture
{
public:
    DX12Texture() = default;
    ~DX12Texture() override;

    /// Allocate an owned texture via D3D12MA.
    void Initialize(DX12Device* device, const RHITextureDesc& desc);

    /// Initialize from an existing ID3D12Resource (e.g. swapchain back buffer).
    /// The texture does NOT own the resource in this case.
    void InitFromExisting(ID3D12Resource* resource, const RHITextureDesc& desc, D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle);

    // ── IRHITexture interface ─────────────────────────────────────────
    const RHITextureDesc& GetDesc() const override
    {
        return m_desc;
    }
    BindlessIndex GetBindlessIndex() const override
    {
        return m_bindlessIndex;
    }

    // ── Internal ──────────────────────────────────────────────────────
    ID3D12Resource* GetD3DResource() const
    {
        return m_resource;
    }
    void SetBindlessIndex(BindlessIndex index)
    {
        m_bindlessIndex = index;
    }
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const
    {
        return m_rtvHandle;
    }

private:
    D3D12MA::Allocation* m_allocation = nullptr;
    ID3D12Resource* m_resource = nullptr; // Owned by allocation, non-owning for swapchain
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandle = {};
    RHITextureDesc m_desc{};
    BindlessIndex m_bindlessIndex = kInvalidBindlessIndex;
    DX12Device* m_device = nullptr;
    bool m_ownsResource = false;
};

} // namespace west::rhi
