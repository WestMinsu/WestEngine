// =============================================================================
// WestEngine - RHI DX12
// DX12 device implementation — factory, debug layer, DRED, adapter selection
// =============================================================================
#pragma once

#include "rhi/dx12/DX12Helpers.h"
#include "rhi/interface/IRHIDevice.h"

namespace west::rhi
{

class DX12Queue;

class DX12Device final : public IRHIDevice
{
public:
    DX12Device() = default;
    ~DX12Device() override;

    /// Initialize the DX12 device with the given configuration.
    /// @return true on success.
    [[nodiscard]] bool Initialize(const RHIDeviceConfig& config);

    // ── IRHIDevice interface ──────────────────────────────────────────
    std::unique_ptr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc) override;
    std::unique_ptr<IRHITexture> CreateTexture(const RHITextureDesc& desc) override;
    std::unique_ptr<IRHISampler> CreateSampler(const RHISamplerDesc& desc) override;
    std::unique_ptr<IRHIPipeline> CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) override;
    std::unique_ptr<IRHIPipeline> CreateComputePipeline(const RHIComputePipelineDesc& desc) override;
    std::unique_ptr<IRHIFence> CreateFence(uint64_t initialValue = 0) override;
    std::unique_ptr<IRHISemaphore> CreateBinarySemaphore() override;
    std::unique_ptr<IRHICommandList> CreateCommandList(RHIQueueType type) override;
    IRHIQueue* GetQueue(RHIQueueType type) override;
    std::unique_ptr<IRHISwapChain> CreateSwapChain(const RHISwapChainDesc& desc) override;

    BindlessIndex RegisterBindlessResource(IRHIBuffer* buffer) override;
    BindlessIndex RegisterBindlessResource(IRHITexture* texture) override;
    void UnregisterBindlessResource(BindlessIndex index) override;

    void WaitIdle() override;
    RHIBackend GetBackend() const override
    {
        return RHIBackend::DX12;
    }
    const char* GetDeviceName() const override;
    RHIDeviceCaps GetCapabilities() const override;

    // ── Internal Accessors (for other DX12 classes) ───────────────────
    ID3D12Device8* GetD3DDevice() const
    {
        return m_device.Get();
    }
    IDXGIFactory7* GetDXGIFactory() const
    {
        return m_factory.Get();
    }

private:
    void EnableDebugLayer(bool enableGBV);
    void EnableDRED();
    void SelectAdapter(uint32_t preferredIndex);
    void CreateDevice();
    void QueryDeviceCaps();
    void CreateQueues();

    ComPtr<IDXGIFactory7> m_factory;
    ComPtr<IDXGIAdapter4> m_adapter;
    ComPtr<ID3D12Device8> m_device;
    DXGI_ADAPTER_DESC3 m_adapterDesc{};

    std::unique_ptr<DX12Queue> m_graphicsQueue;
    // TODO(minsu): Phase 3 — Compute and Copy queues

    RHIDeviceCaps m_caps{};
    std::string m_deviceName;
    bool m_dredEnabled = false;
};

} // namespace west::rhi
