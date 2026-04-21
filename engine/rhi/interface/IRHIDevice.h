// =============================================================================
// WestEngine - RHI Interface
// Abstract device interface — resource creation, queue access, bindless pool
// =============================================================================
#pragma once

#include "rhi/interface/RHIDescriptors.h"
#include "rhi/interface/RHIEnums.h"

#include <functional>
#include <memory>

namespace west::rhi
{

class IRHIBuffer;
class IRHITexture;
class IRHISampler;
class IRHIPipeline;
class IRHIFence;
class IRHISemaphore;
class IRHICommandList;
class IRHIQueue;
class IRHISwapChain;

class IRHIDevice
{
public:
    virtual ~IRHIDevice() = default;

    // ── Resource Creation ─────────────────────────────────────────────
    virtual std::unique_ptr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc) = 0;
    virtual std::unique_ptr<IRHITexture> CreateTexture(const RHITextureDesc& desc) = 0;
    virtual std::unique_ptr<IRHIBuffer> CreateTransientBuffer(const RHIBufferDesc& desc, uint32_t aliasSlot) = 0;
    virtual std::unique_ptr<IRHITexture> CreateTransientTexture(const RHITextureDesc& desc, uint32_t aliasSlot) = 0;
    virtual std::unique_ptr<IRHISampler> CreateSampler(const RHISamplerDesc& desc) = 0;

    // ── Pipeline ──────────────────────────────────────────────────────
    virtual std::unique_ptr<IRHIPipeline> CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) = 0;
    virtual std::unique_ptr<IRHIPipeline> CreateComputePipeline(const RHIComputePipelineDesc& desc) = 0;

    // ── Synchronization ───────────────────────────────────────────────
    virtual std::unique_ptr<IRHIFence> CreateFence(uint64_t initialValue = 0) = 0;
    virtual std::unique_ptr<IRHISemaphore> CreateBinarySemaphore() = 0;

    // ── Command ───────────────────────────────────────────────────────
    virtual std::unique_ptr<IRHICommandList> CreateCommandList(RHIQueueType type) = 0;

    // ── Queue ─────────────────────────────────────────────────────────
    virtual IRHIQueue* GetQueue(RHIQueueType type) = 0;

    // ── SwapChain ─────────────────────────────────────────────────────
    virtual std::unique_ptr<IRHISwapChain> CreateSwapChain(const RHISwapChainDesc& desc) = 0;

    // ── Bindless ──────────────────────────────────────────────────────
    virtual BindlessIndex RegisterBindlessResource(IRHIBuffer* buffer) = 0;
    virtual BindlessIndex RegisterBindlessResource(IRHITexture* texture) = 0;
    virtual BindlessIndex RegisterBindlessResource(IRHISampler* sampler) = 0;
    virtual void UnregisterBindlessResource(BindlessIndex index) = 0;

    // ── Utility ───────────────────────────────────────────────────────
    virtual void WaitIdle() = 0;

    // ── Device Info ───────────────────────────────────────────────────
    virtual RHIBackend GetBackend() const = 0;
    virtual const char* GetDeviceName() const = 0;
    virtual RHIDeviceCaps GetCapabilities() const = 0;

    // ── Memory Management ─────────────────────────────────────────────
    virtual void EnqueueDeferredDeletion(std::function<void()> deleter, uint64_t fenceValue) = 0;
    virtual void FlushDeferredDeletions(uint64_t completedFenceValue) = 0;
    virtual void FlushAllDeferredDeletions() = 0;

    // Application sets this at the start of the frame so resources know when they are safe to delete.
    virtual void SetCurrentFrameFenceValue(uint64_t fenceValue) = 0;
    virtual uint64_t GetCurrentFrameFenceValue() const = 0;
};

} // namespace west::rhi
