// =============================================================================
// WestEngine - RHI DX12
// DX12 texture — minimal wrapper for SwapChain back buffers (Phase 1)
// =============================================================================
#pragma once

#include "rhi/dx12/DX12Helpers.h"
#include "rhi/interface/IRHITexture.h"

namespace west::rhi
{

class DX12Texture final : public IRHITexture
{
public:
    DX12Texture() = default;
    ~DX12Texture() override = default;

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
    D3D12_CPU_DESCRIPTOR_HANDLE GetRTV() const
    {
        return m_rtvHandle;
    }

private:
    ID3D12Resource* m_resource = nullptr; // Non-owning for swapchain
    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandle = {};
    RHITextureDesc m_desc{};
    BindlessIndex m_bindlessIndex = kInvalidBindlessIndex;
};

} // namespace west::rhi
