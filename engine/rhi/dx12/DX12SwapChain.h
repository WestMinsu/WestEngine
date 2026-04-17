// =============================================================================
// WestEngine - RHI DX12
// DX12 swap chain — DXGI presentation with Flip Model
// =============================================================================
#pragma once

#include "rhi/dx12/DX12Helpers.h"
#include "rhi/dx12/DX12Texture.h"
#include "rhi/interface/IRHISwapChain.h"

#include <vector>

namespace west::rhi
{

class DX12Device;

class DX12SwapChain final : public IRHISwapChain
{
public:
    DX12SwapChain() = default;
    ~DX12SwapChain() override;

    /// Initialize the swap chain.
    void Initialize(DX12Device* device, const RHISwapChainDesc& desc);

    // ── IRHISwapChain interface ───────────────────────────────────────
    uint32_t AcquireNextImage(IRHISemaphore* acquireSemaphore = nullptr) override;
    void Present(IRHISemaphore* presentSemaphore = nullptr) override;
    IRHITexture* GetCurrentBackBuffer() override;
    void Resize(uint32_t width, uint32_t height) override;
    uint32_t GetCurrentIndex() const override
    {
        return m_currentIndex;
    }
    uint32_t GetBufferCount() const override
    {
        return m_bufferCount;
    }
    RHIFormat GetFormat() const override
    {
        return m_format;
    }

private:
    void CreateRTVHeap();
    void AcquireBackBuffers();
    void ReleaseBackBuffers();

    DX12Device* m_device = nullptr;
    ComPtr<IDXGISwapChain4> m_swapChain;
    ComPtr<ID3D12DescriptorHeap> m_rtvHeap;

    std::vector<ComPtr<ID3D12Resource>> m_backBufferResources;
    std::vector<DX12Texture> m_backBufferTextures;

    uint32_t m_currentIndex = 0;
    uint32_t m_bufferCount = 0;
    uint32_t m_rtvDescriptorSize = 0;
    RHIFormat m_format = RHIFormat::RGBA8_UNORM;
    bool m_vsync = false;
    bool m_tearingSupport = false;
};

} // namespace west::rhi
