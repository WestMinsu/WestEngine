// =============================================================================
// WestEngine - RHI DX12
// DX12 device implementation — factory, debug layer, DRED, adapter selection
// =============================================================================
#pragma once

#include "rhi/dx12/DX12Helpers.h"
#include "rhi/common/BindlessPool.h"
#include "rhi/common/DeferredDeletionQueue.h"
#include "rhi/interface/IRHIDevice.h"

#include <mutex>
#include <utility>
#include <vector>

namespace D3D12MA
{
class Allocation;
}

namespace west::rhi
{

class DX12Queue;
class DX12MemoryAllocator;

class DX12Device final : public IRHIDevice
{
public:
    DX12Device();
    ~DX12Device() override;

    /// Initialize the DX12 device with the given configuration.
    /// @return true on success.
    [[nodiscard]] bool Initialize(const RHIDeviceConfig& config);

    // ── IRHIDevice interface ──────────────────────────────────────────
    std::unique_ptr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc) override;
    std::unique_ptr<IRHITexture> CreateTexture(const RHITextureDesc& desc) override;
    std::unique_ptr<IRHIBuffer> CreateTransientBuffer(const RHIBufferDesc& desc, uint32_t aliasSlot) override;
    std::unique_ptr<IRHITexture> CreateTransientTexture(const RHITextureDesc& desc, uint32_t aliasSlot) override;
    std::unique_ptr<IRHISampler> CreateSampler(const RHISamplerDesc& desc) override;
    std::unique_ptr<IRHIPipeline> CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) override;
    std::unique_ptr<IRHIPipeline> CreateComputePipeline(const RHIComputePipelineDesc& desc) override;
    std::unique_ptr<IRHIFence> CreateFence(uint64_t initialValue = 0) override;
    std::unique_ptr<IRHISemaphore> CreateBinarySemaphore() override;
    std::unique_ptr<IRHICommandList> CreateCommandList(RHIQueueType type) override;
    IRHIQueue* GetQueue(RHIQueueType type) override;
    std::unique_ptr<IRHISwapChain> CreateSwapChain(const RHISwapChainDesc& desc) override;

    BindlessIndex RegisterBindlessResource(IRHIBuffer* buffer, bool writable = false) override;
    BindlessIndex RegisterBindlessResource(IRHITexture* texture) override;
    BindlessIndex RegisterBindlessResource(IRHISampler* sampler) override;
    void UnregisterBindlessResource(BindlessIndex index) override;

    void WaitIdle() override;
    RHIBackend GetBackend() const override
    {
        return RHIBackend::DX12;
    }
    const char* GetDeviceName() const override;
    RHIDeviceCaps GetCapabilities() const override;

    // ── Internal Accessors (for other DX12 classes) ───────────────────
    ID3D12Device* GetD3DDevice() const
    {
        return m_device.Get();
    }
    IDXGIFactory7* GetDXGIFactory() const
    {
        return m_factory.Get();
    }
    DX12MemoryAllocator* GetMemoryAllocator()
    {
        return m_memoryAllocator.get();
    }
    ID3D12DescriptorHeap* GetResourceDescriptorHeap() const
    {
        return m_resourceDescriptorHeap.Get();
    }
    ID3D12DescriptorHeap* GetSamplerDescriptorHeap() const
    {
        return m_samplerDescriptorHeap.Get();
    }
    ID3D12RootSignature* GetGlobalRootSignature() const
    {
        return m_globalRootSignature.Get();
    }

    // ── Memory Management ─────────────────────────────────────────────
    void EnqueueDeferredDeletion(std::function<void()> deleter, uint64_t fenceValue) override
    {
        m_deletionQueue.Enqueue(std::move(deleter), fenceValue);
    }
    void FlushDeferredDeletions(uint64_t completedFenceValue) override
    {
        m_deletionQueue.Flush(completedFenceValue);
    }
    void FlushAllDeferredDeletions() override
    {
        m_deletionQueue.FlushAll();
    }
    void SetCurrentFrameFenceValue(uint64_t fenceValue) override
    {
        m_currentFenceValue = fenceValue;
    }
    uint64_t GetCurrentFrameFenceValue() const override
    {
        return m_currentFenceValue;
    }

private:
    struct TransientTextureAliasEntry
    {
        RHITextureDesc desc{};
        std::weak_ptr<D3D12MA::Allocation> allocation;
        bool valid = false;
    };

    [[nodiscard]] bool EnableDebugLayer(bool enableGBV);
    void EnableDRED();
    void SelectAdapter(uint32_t preferredIndex);
    void CreateDevice();
    void QueryDeviceCaps();
    void CreateGlobalRootSignature();
    void CreateQueues();
    void CreateBindlessHeaps();

    D3D12_CPU_DESCRIPTOR_HANDLE GetResourceDescriptorCPU(BindlessIndex index) const;
    D3D12_CPU_DESCRIPTOR_HANDLE GetSamplerDescriptorCPU(BindlessIndex index) const;

    ComPtr<IDXGIFactory7> m_factory;
    ComPtr<IDXGIAdapter4> m_adapter;
    ComPtr<ID3D12Device> m_device;
    ComPtr<ID3D12RootSignature> m_globalRootSignature;
    DXGI_ADAPTER_DESC3 m_adapterDesc{};

    std::unique_ptr<DX12Queue> m_graphicsQueue;
    std::unique_ptr<DX12Queue> m_computeQueue;
    std::unique_ptr<DX12Queue> m_copyQueue;
    std::unique_ptr<DX12MemoryAllocator> m_memoryAllocator;
    std::vector<TransientTextureAliasEntry> m_transientTextureAliases;
    std::mutex m_transientTextureMutex;

    static constexpr uint32_t kBindlessCapacity = 4096;
    ComPtr<ID3D12DescriptorHeap> m_resourceDescriptorHeap;
    ComPtr<ID3D12DescriptorHeap> m_samplerDescriptorHeap;
    uint32_t m_resourceDescriptorSize = 0;
    uint32_t m_samplerDescriptorSize = 0;
    BindlessPool m_bindlessPool;
    std::vector<uint8> m_bindlessPendingFree;
    std::mutex m_bindlessMutex;

    RHIDeviceCaps m_caps{};
    std::string m_deviceName;
    bool m_dredEnabled = false;

    DeferredDeletionQueue m_deletionQueue;
    uint64_t m_currentFenceValue = 0;
};

} // namespace west::rhi
