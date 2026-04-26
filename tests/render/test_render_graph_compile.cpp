// =============================================================================
// WestEngine Tests - Render
// Render Graph compile behavior
// =============================================================================
#include "render/RenderGraph/RenderGraph.h"
#include "render/RenderGraph/CommandListPool.h"
#include "render/RenderGraph/RenderGraphCompiler.h"
#include "render/RenderGraph/TransientResourcePool.h"
#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHIFence.h"
#include "rhi/interface/IRHIPipeline.h"
#include "rhi/interface/IRHIQueue.h"
#include "rhi/interface/IRHISampler.h"
#include "rhi/interface/IRHISemaphore.h"
#include "rhi/interface/IRHISwapChain.h"
#include "TestAssert.h"

#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <vector>

using namespace west;

namespace
{

class FakeTexture final : public rhi::IRHITexture
{
public:
    explicit FakeTexture(rhi::RHITextureDesc desc)
        : m_desc(desc)
    {
    }

    const rhi::RHITextureDesc& GetDesc() const override
    {
        return m_desc;
    }

    rhi::BindlessIndex GetBindlessIndex() const override
    {
        return rhi::kInvalidBindlessIndex;
    }

private:
    rhi::RHITextureDesc m_desc{};
};

class FakeTimestampQueryPool final : public rhi::IRHITimestampQueryPool
{
public:
    explicit FakeTimestampQueryPool(uint32_t queryCount, rhi::RHIQueueType queueType)
    {
        m_desc.queryCount = queryCount;
        m_desc.queueType = queueType;
        m_desc.debugName = "FakeTimestampQueryPool";
    }

    const rhi::RHITimestampQueryPoolDesc& GetDesc() const override
    {
        return m_desc;
    }

    float GetTimestampPeriodNanoseconds() const override
    {
        return 1.0f;
    }

    bool ReadTimestamps(uint32_t, uint32_t queryCount, std::span<uint64_t> timestamps) override
    {
        for (uint32_t index = 0; index < queryCount && index < timestamps.size(); ++index)
        {
            timestamps[index] = index;
        }
        return true;
    }

private:
    rhi::RHITimestampQueryPoolDesc m_desc{};
};

class TestPass final : public render::RenderGraphPass
{
public:
    using SetupFn = std::function<void(render::RenderGraphBuilder&)>;

    TestPass(const char* debugName, rhi::RHIQueueType queueType, SetupFn setup, bool hasSideEffects = false)
        : m_debugName(debugName)
        , m_queueType(queueType)
        , m_setup(std::move(setup))
        , m_hasSideEffects(hasSideEffects)
    {
    }

    void Setup(render::RenderGraphBuilder& builder) override
    {
        m_setup(builder);
    }

    void Execute(render::RenderGraphContext&, rhi::IRHICommandList&) override
    {
    }

    bool HasSideEffects() const override
    {
        return m_hasSideEffects;
    }

    rhi::RHIQueueType GetQueueType() const override
    {
        return m_queueType;
    }

    const char* GetDebugName() const override
    {
        return m_debugName;
    }

private:
    const char* m_debugName = "TestPass";
    rhi::RHIQueueType m_queueType = rhi::RHIQueueType::Graphics;
    SetupFn m_setup;
    bool m_hasSideEffects = false;
};

class FakeDevice final : public rhi::IRHIDevice
{
public:
    std::unique_ptr<rhi::IRHIBuffer> CreateBuffer(const rhi::RHIBufferDesc&) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHITexture> CreateTexture(const rhi::RHITextureDesc& desc) override
    {
        return std::make_unique<FakeTexture>(desc);
    }

    std::unique_ptr<rhi::IRHIBuffer> CreateTransientBuffer(const rhi::RHIBufferDesc&, uint32_t) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHITexture> CreateTransientTexture(const rhi::RHITextureDesc& desc, uint32_t aliasSlot) override
    {
        ++transientTextureCreateCount;
        observedAliasSlots.push_back(aliasSlot);
        return std::make_unique<FakeTexture>(desc);
    }

    std::unique_ptr<rhi::IRHISampler> CreateSampler(const rhi::RHISamplerDesc&) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHIPipeline> CreateGraphicsPipeline(const rhi::RHIGraphicsPipelineDesc&) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHIPipeline> CreateComputePipeline(const rhi::RHIComputePipelineDesc&) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHIFence> CreateFence(uint64_t = 0) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHISemaphore> CreateBinarySemaphore() override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHITimestampQueryPool> CreateTimestampQueryPool(
        const rhi::RHITimestampQueryPoolDesc&) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHICommandList> CreateCommandList(rhi::RHIQueueType) override
    {
        return nullptr;
    }

    rhi::IRHIQueue* GetQueue(rhi::RHIQueueType) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHISwapChain> CreateSwapChain(const rhi::RHISwapChainDesc&) override
    {
        return nullptr;
    }

    rhi::BindlessIndex RegisterBindlessResource(
        rhi::IRHIBuffer*,
        rhi::RHIBindlessBufferView = rhi::RHIBindlessBufferView::ReadOnly) override
    {
        return rhi::kInvalidBindlessIndex;
    }

    rhi::BindlessIndex RegisterBindlessResource(rhi::IRHITexture*) override
    {
        return rhi::kInvalidBindlessIndex;
    }

    rhi::BindlessIndex RegisterBindlessResource(rhi::IRHISampler*) override
    {
        return rhi::kInvalidBindlessIndex;
    }

    void UnregisterBindlessResource(rhi::IRHIBuffer*) override
    {
    }

    void UnregisterBindlessResource(rhi::IRHITexture*) override
    {
    }

    void UnregisterBindlessResource(rhi::IRHISampler*) override
    {
    }

    void WaitIdle() override
    {
    }

    rhi::RHIBackend GetBackend() const override
    {
        return rhi::RHIBackend::DX12;
    }

    const char* GetDeviceName() const override
    {
        return "FakeDevice";
    }

    rhi::RHIDeviceCaps GetCapabilities() const override
    {
        return {};
    }

    void EnqueueDeferredDeletion(std::function<void()>, uint64_t) override
    {
    }

    void FlushDeferredDeletions(uint64_t) override
    {
    }

    void FlushAllDeferredDeletions() override
    {
    }

    void SetCurrentFrameFenceValue(uint64_t fenceValue) override
    {
        currentFenceValue = fenceValue;
    }

    uint64_t GetCurrentFrameFenceValue() const override
    {
        return currentFenceValue;
    }

    uint32_t transientTextureCreateCount = 0;
    uint64_t currentFenceValue = 0;
    std::vector<uint32_t> observedAliasSlots;
};

class FakeCommandList final : public rhi::IRHICommandList
{
public:
    explicit FakeCommandList(rhi::RHIQueueType queueType)
        : m_queueType(queueType)
    {
    }

    void Begin() override {}
    void End() override {}
    void Reset() override {}
    void SetPipeline(rhi::IRHIPipeline*) override {}
    void SetPushConstants(const void*, uint32_t) override {}
    void SetVertexBuffer(uint32_t, rhi::IRHIBuffer*, uint64_t) override {}
    void SetIndexBuffer(rhi::IRHIBuffer*, rhi::RHIFormat, uint64_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void Draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
    void DrawIndexedIndirectCount(rhi::IRHIBuffer*, uint64_t, rhi::IRHIBuffer*, uint64_t, uint32_t, uint32_t) override {}
    void Dispatch(uint32_t, uint32_t, uint32_t) override {}
    void BeginRenderPass(const rhi::RHIRenderPassDesc&) override {}
    void EndRenderPass() override {}
    void ResourceBarrier(const rhi::RHIBarrierDesc&) override {}
    void ResourceBarriers(std::span<const rhi::RHIBarrierDesc>) override {}
    void CopyBuffer(rhi::IRHIBuffer*, uint64_t, rhi::IRHIBuffer*, uint64_t, uint64_t) override {}
    void CopyBufferToTexture(rhi::IRHIBuffer*, rhi::IRHITexture*, const rhi::RHICopyRegion&) override {}
    void ResetTimestampQueries(rhi::IRHITimestampQueryPool*, uint32_t, uint32_t) override {}
    void WriteTimestamp(rhi::IRHITimestampQueryPool*, uint32_t) override {}
    void ResolveTimestampQueries(rhi::IRHITimestampQueryPool*, uint32_t, uint32_t) override {}

    rhi::RHIQueueType GetQueueType() const override
    {
        return m_queueType;
    }

private:
    rhi::RHIQueueType m_queueType = rhi::RHIQueueType::Graphics;
};

class PoolFakeDevice final : public rhi::IRHIDevice
{
public:
    std::unique_ptr<rhi::IRHIBuffer> CreateBuffer(const rhi::RHIBufferDesc&) override { return nullptr; }
    std::unique_ptr<rhi::IRHITexture> CreateTexture(const rhi::RHITextureDesc&) override { return nullptr; }
    std::unique_ptr<rhi::IRHIBuffer> CreateTransientBuffer(const rhi::RHIBufferDesc&, uint32_t) override { return nullptr; }
    std::unique_ptr<rhi::IRHITexture> CreateTransientTexture(const rhi::RHITextureDesc&, uint32_t) override { return nullptr; }
    std::unique_ptr<rhi::IRHISampler> CreateSampler(const rhi::RHISamplerDesc&) override { return nullptr; }
    std::unique_ptr<rhi::IRHIPipeline> CreateGraphicsPipeline(const rhi::RHIGraphicsPipelineDesc&) override { return nullptr; }
    std::unique_ptr<rhi::IRHIPipeline> CreateComputePipeline(const rhi::RHIComputePipelineDesc&) override { return nullptr; }
    std::unique_ptr<rhi::IRHIFence> CreateFence(uint64_t = 0) override { return nullptr; }
    std::unique_ptr<rhi::IRHISemaphore> CreateBinarySemaphore() override { return nullptr; }
    std::unique_ptr<rhi::IRHITimestampQueryPool> CreateTimestampQueryPool(
        const rhi::RHITimestampQueryPoolDesc&) override { return nullptr; }

    std::unique_ptr<rhi::IRHICommandList> CreateCommandList(rhi::RHIQueueType type) override
    {
        ++createCommandListCount;
        return std::make_unique<FakeCommandList>(type);
    }

    rhi::IRHIQueue* GetQueue(rhi::RHIQueueType) override { return nullptr; }
    std::unique_ptr<rhi::IRHISwapChain> CreateSwapChain(const rhi::RHISwapChainDesc&) override { return nullptr; }
    rhi::BindlessIndex RegisterBindlessResource(
        rhi::IRHIBuffer*,
        rhi::RHIBindlessBufferView = rhi::RHIBindlessBufferView::ReadOnly) override { return rhi::kInvalidBindlessIndex; }
    rhi::BindlessIndex RegisterBindlessResource(rhi::IRHITexture*) override { return rhi::kInvalidBindlessIndex; }
    rhi::BindlessIndex RegisterBindlessResource(rhi::IRHISampler*) override { return rhi::kInvalidBindlessIndex; }
    void UnregisterBindlessResource(rhi::IRHIBuffer*) override {}
    void UnregisterBindlessResource(rhi::IRHITexture*) override {}
    void UnregisterBindlessResource(rhi::IRHISampler*) override {}
    void WaitIdle() override {}
    rhi::RHIBackend GetBackend() const override { return rhi::RHIBackend::DX12; }
    const char* GetDeviceName() const override { return "PoolFakeDevice"; }
    rhi::RHIDeviceCaps GetCapabilities() const override { return {}; }
    void EnqueueDeferredDeletion(std::function<void()>, uint64_t) override {}
    void FlushDeferredDeletions(uint64_t) override {}
    void FlushAllDeferredDeletions() override {}
    void SetCurrentFrameFenceValue(uint64_t) override {}
    uint64_t GetCurrentFrameFenceValue() const override { return 0; }

    uint32_t createCommandListCount = 0;
};

class FakeFence final : public rhi::IRHIFence
{
public:
    uint64_t GetCompletedValue() const override
    {
        return completedValue;
    }

    void Wait(uint64_t value, uint64_t) override
    {
        completedValue = (std::max)(completedValue, value);
    }

    uint64_t AdvanceValue() override
    {
        return ++nextValue;
    }

    uint64_t completedValue = 0;
    uint64_t nextValue = 0;
};

class RecordingCommandList final : public rhi::IRHICommandList
{
public:
    explicit RecordingCommandList(rhi::RHIQueueType queueType)
        : m_queueType(queueType)
    {
    }

    void Begin() override {}
    void End() override {}
    void Reset() override {}
    void SetPipeline(rhi::IRHIPipeline*) override {}
    void SetPushConstants(const void*, uint32_t) override {}
    void SetVertexBuffer(uint32_t, rhi::IRHIBuffer*, uint64_t) override {}
    void SetIndexBuffer(rhi::IRHIBuffer*, rhi::RHIFormat, uint64_t) override {}
    void SetViewport(float, float, float, float, float, float) override {}
    void SetScissor(int32_t, int32_t, uint32_t, uint32_t) override {}
    void Draw(uint32_t, uint32_t, uint32_t, uint32_t) override {}
    void DrawIndexed(uint32_t, uint32_t, uint32_t, int32_t, uint32_t) override {}
    void DrawIndexedIndirectCount(rhi::IRHIBuffer*, uint64_t, rhi::IRHIBuffer*, uint64_t, uint32_t, uint32_t) override {}
    void Dispatch(uint32_t, uint32_t, uint32_t) override {}
    void BeginRenderPass(const rhi::RHIRenderPassDesc&) override {}
    void EndRenderPass() override {}

    void ResourceBarrier(const rhi::RHIBarrierDesc&) override
    {
        ++singleBarrierCalls;
    }

    void ResourceBarriers(std::span<const rhi::RHIBarrierDesc> descs) override
    {
        barrierBatchSizes.push_back(static_cast<uint32_t>(descs.size()));
    }

    void CopyBuffer(rhi::IRHIBuffer*, uint64_t, rhi::IRHIBuffer*, uint64_t, uint64_t) override {}
    void CopyBufferToTexture(rhi::IRHIBuffer*, rhi::IRHITexture*, const rhi::RHICopyRegion&) override {}
    void ResetTimestampQueries(rhi::IRHITimestampQueryPool*, uint32_t firstQuery, uint32_t queryCount) override
    {
        ++timestampResetCalls;
        lastResetFirstQuery = firstQuery;
        lastResetQueryCount = queryCount;
    }

    void WriteTimestamp(rhi::IRHITimestampQueryPool*, uint32_t index) override
    {
        ++timestampWriteCalls;
        writtenTimestampIndices.push_back(index);
    }

    void ResolveTimestampQueries(rhi::IRHITimestampQueryPool*, uint32_t firstQuery, uint32_t queryCount) override
    {
        ++timestampResolveCalls;
        lastResolveFirstQuery = firstQuery;
        lastResolveQueryCount = queryCount;
    }

    rhi::RHIQueueType GetQueueType() const override
    {
        return m_queueType;
    }

    uint32_t singleBarrierCalls = 0;
    std::vector<uint32_t> barrierBatchSizes;
    uint32_t timestampResetCalls = 0;
    uint32_t timestampWriteCalls = 0;
    uint32_t timestampResolveCalls = 0;
    uint32_t lastResetFirstQuery = 0;
    uint32_t lastResetQueryCount = 0;
    uint32_t lastResolveFirstQuery = 0;
    uint32_t lastResolveQueryCount = 0;
    std::vector<uint32_t> writtenTimestampIndices;

private:
    rhi::RHIQueueType m_queueType = rhi::RHIQueueType::Graphics;
};

class RecordingQueue final : public rhi::IRHIQueue
{
public:
    explicit RecordingQueue(rhi::RHIQueueType type)
        : m_type(type)
    {
    }

    void Submit(const rhi::RHISubmitInfo& info) override
    {
        ++submitCount;
        lastSubmitCommandListCount = static_cast<uint32_t>(info.commandLists.size());
        lastTimelineWaitStages.clear();
        lastTimelineWaitStages.reserve(info.timelineWaits.size());
        for (const rhi::RHITimelineWaitDesc& wait : info.timelineWaits)
        {
            lastTimelineWaitStages.push_back(wait.stageMask);
        }
    }

    rhi::RHIQueueType GetType() const override
    {
        return m_type;
    }

    uint32_t submitCount = 0;
    uint32_t lastSubmitCommandListCount = 0;
    std::vector<rhi::RHIPipelineStage> lastTimelineWaitStages;

private:
    rhi::RHIQueueType m_type = rhi::RHIQueueType::Graphics;
};

class ExecuteFakeDevice final : public rhi::IRHIDevice
{
public:
    std::unique_ptr<rhi::IRHIBuffer> CreateBuffer(const rhi::RHIBufferDesc&) override { return nullptr; }
    std::unique_ptr<rhi::IRHITexture> CreateTexture(const rhi::RHITextureDesc&) override { return nullptr; }
    std::unique_ptr<rhi::IRHIBuffer> CreateTransientBuffer(const rhi::RHIBufferDesc&, uint32_t) override { return nullptr; }
    std::unique_ptr<rhi::IRHITexture> CreateTransientTexture(const rhi::RHITextureDesc&, uint32_t) override { return nullptr; }
    std::unique_ptr<rhi::IRHISampler> CreateSampler(const rhi::RHISamplerDesc&) override { return nullptr; }
    std::unique_ptr<rhi::IRHIPipeline> CreateGraphicsPipeline(const rhi::RHIGraphicsPipelineDesc&) override { return nullptr; }
    std::unique_ptr<rhi::IRHIPipeline> CreateComputePipeline(const rhi::RHIComputePipelineDesc&) override { return nullptr; }
    std::unique_ptr<rhi::IRHIFence> CreateFence(uint64_t = 0) override { return nullptr; }
    std::unique_ptr<rhi::IRHISemaphore> CreateBinarySemaphore() override { return nullptr; }
    std::unique_ptr<rhi::IRHITimestampQueryPool> CreateTimestampQueryPool(
        const rhi::RHITimestampQueryPoolDesc&) override { return nullptr; }

    std::unique_ptr<rhi::IRHICommandList> CreateCommandList(rhi::RHIQueueType type) override
    {
        auto commandList = std::make_unique<RecordingCommandList>(type);
        lastCommandList = commandList.get();
        return commandList;
    }

    rhi::IRHIQueue* GetQueue(rhi::RHIQueueType type) override
    {
        switch (type)
        {
        case rhi::RHIQueueType::Graphics:
            return &graphicsQueue;
        case rhi::RHIQueueType::Compute:
            return &computeQueue;
        case rhi::RHIQueueType::Copy:
            return &copyQueue;
        }

        return &graphicsQueue;
    }

    std::unique_ptr<rhi::IRHISwapChain> CreateSwapChain(const rhi::RHISwapChainDesc&) override { return nullptr; }
    rhi::BindlessIndex RegisterBindlessResource(
        rhi::IRHIBuffer*,
        rhi::RHIBindlessBufferView = rhi::RHIBindlessBufferView::ReadOnly) override { return rhi::kInvalidBindlessIndex; }
    rhi::BindlessIndex RegisterBindlessResource(rhi::IRHITexture*) override { return rhi::kInvalidBindlessIndex; }
    rhi::BindlessIndex RegisterBindlessResource(rhi::IRHISampler*) override { return rhi::kInvalidBindlessIndex; }
    void UnregisterBindlessResource(rhi::IRHIBuffer*) override {}
    void UnregisterBindlessResource(rhi::IRHITexture*) override {}
    void UnregisterBindlessResource(rhi::IRHISampler*) override {}
    void WaitIdle() override {}
    rhi::RHIBackend GetBackend() const override { return rhi::RHIBackend::DX12; }
    const char* GetDeviceName() const override { return "ExecuteFakeDevice"; }
    rhi::RHIDeviceCaps GetCapabilities() const override { return {}; }
    void EnqueueDeferredDeletion(std::function<void()> deleter, uint64_t) override
    {
        deferredDeletions.push_back(std::move(deleter));
    }

    void FlushDeferredDeletions(uint64_t) override
    {
    }

    void FlushAllDeferredDeletions() override
    {
        for (auto& deleter : deferredDeletions)
        {
            deleter();
        }
        deferredDeletions.clear();
    }
    void SetCurrentFrameFenceValue(uint64_t) override {}
    uint64_t GetCurrentFrameFenceValue() const override { return 0; }

    RecordingCommandList* lastCommandList = nullptr;
    RecordingQueue graphicsQueue{rhi::RHIQueueType::Graphics};
    RecordingQueue computeQueue{rhi::RHIQueueType::Compute};
    RecordingQueue copyQueue{rhi::RHIQueueType::Copy};
    std::vector<std::function<void()>> deferredDeletions;
};

[[nodiscard]] bool HasTransition(const std::vector<render::CompiledBarrier>& barriers, uint32_t resourceIndex,
                                 rhi::RHIResourceState before, rhi::RHIResourceState after)
{
    for (const render::CompiledBarrier& barrier : barriers)
    {
        if (barrier.type == rhi::RHIBarrierDesc::Type::Transition &&
            barrier.resourceIndex == resourceIndex &&
            barrier.stateBefore == before &&
            barrier.stateAfter == after)
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool HasTransitionWithStages(const std::vector<render::CompiledBarrier>& barriers,
                                           uint32_t resourceIndex,
                                           rhi::RHIResourceState before,
                                           rhi::RHIResourceState after,
                                           rhi::RHIPipelineStage srcStage,
                                           rhi::RHIPipelineStage dstStage)
{
    for (const render::CompiledBarrier& barrier : barriers)
    {
        if (barrier.type == rhi::RHIBarrierDesc::Type::Transition &&
            barrier.resourceIndex == resourceIndex &&
            barrier.stateBefore == before &&
            barrier.stateAfter == after &&
            barrier.srcStageMask == srcStage &&
            barrier.dstStageMask == dstStage)
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool HasAliasingBarrier(const std::vector<render::CompiledBarrier>& barriers, uint32_t beforeResource,
                                      uint32_t afterResource)
{
    for (const render::CompiledBarrier& barrier : barriers)
    {
        if (barrier.type == rhi::RHIBarrierDesc::Type::Aliasing &&
            barrier.aliasBeforeResourceIndex == beforeResource &&
            barrier.aliasAfterResourceIndex == afterResource)
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool HasAliasingBarrierWithStates(const std::vector<render::CompiledBarrier>& barriers,
                                                uint32_t beforeResource, uint32_t afterResource,
                                                rhi::RHIResourceState beforeState, rhi::RHIResourceState afterState,
                                                rhi::RHIPipelineStage beforeStage,
                                                rhi::RHIPipelineStage afterStage)
{
    for (const render::CompiledBarrier& barrier : barriers)
    {
        if (barrier.type == rhi::RHIBarrierDesc::Type::Aliasing &&
            barrier.aliasBeforeResourceIndex == beforeResource &&
            barrier.aliasAfterResourceIndex == afterResource &&
            barrier.stateBefore == beforeState &&
            barrier.stateAfter == afterState &&
            barrier.srcStageMask == beforeStage &&
            barrier.dstStageMask == afterStage)
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] bool HasUAVBarrier(const std::vector<render::CompiledBarrier>& barriers, uint32_t resourceIndex)
{
    for (const render::CompiledBarrier& barrier : barriers)
    {
        if (barrier.type == rhi::RHIBarrierDesc::Type::UAV &&
            barrier.resourceIndex == resourceIndex)
        {
            return true;
        }
    }

    return false;
}

void TestImportedBackBufferTransitions()
{
    render::RenderGraph graph;

    rhi::RHITextureDesc backBufferDesc{};
    backBufferDesc.width = 1920;
    backBufferDesc.height = 1080;
    backBufferDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    backBufferDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::Present;
    FakeTexture backBuffer(backBufferDesc);

    const render::TextureHandle backBufferHandle =
        graph.ImportTexture(&backBuffer, rhi::RHIResourceState::Present, rhi::RHIResourceState::Present, "BackBuffer");

    TestPass pass("BackBufferWrite", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(backBufferHandle, rhi::RHIResourceState::RenderTarget);
    });

    graph.AddPass(pass);
    graph.Compile();

    const render::CompiledRenderGraph& compiledGraph = graph.GetCompiledGraph();
    assert(compiledGraph.passes.size() == 1);
    assert(HasTransition(compiledGraph.passes[0].preBarriers, backBufferHandle.index,
                         rhi::RHIResourceState::Present, rhi::RHIResourceState::RenderTarget));
    assert(HasTransition(compiledGraph.finalBarriers, backBufferHandle.index,
                         rhi::RHIResourceState::RenderTarget, rhi::RHIResourceState::Present));
}

void TestImportedStateRefreshUpdatesCompiledBarriers()
{
    render::RenderGraph graph;

    rhi::RHITextureDesc backBufferDesc{};
    backBufferDesc.width = 1920;
    backBufferDesc.height = 1080;
    backBufferDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    backBufferDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::Present;
    FakeTexture backBuffer(backBufferDesc);

    const render::TextureHandle backBufferHandle =
        graph.ImportTexture(&backBuffer, rhi::RHIResourceState::Present, rhi::RHIResourceState::Present, "BackBuffer");

    TestPass pass("BackBufferWrite", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(backBufferHandle, rhi::RHIResourceState::RenderTarget);
    });

    graph.AddPass(pass);
    graph.Compile();
    assert(HasTransition(graph.GetCompiledGraph().passes[0].preBarriers, backBufferHandle.index,
                         rhi::RHIResourceState::Present, rhi::RHIResourceState::RenderTarget));

    graph.UpdateImportedTexture(backBufferHandle, &backBuffer, rhi::RHIResourceState::Undefined,
                                rhi::RHIResourceState::Present, "BackBuffer");

    const render::CompiledRenderGraph& refreshedGraph = graph.GetCompiledGraph();
    assert(HasTransition(refreshedGraph.passes[0].preBarriers, backBufferHandle.index,
                         rhi::RHIResourceState::Undefined, rhi::RHIResourceState::RenderTarget));
    assert(!HasTransition(refreshedGraph.passes[0].preBarriers, backBufferHandle.index,
                          rhi::RHIResourceState::Present, rhi::RHIResourceState::RenderTarget));
    assert(HasTransition(refreshedGraph.queueBatches[0].postBarriers, backBufferHandle.index,
                         rhi::RHIResourceState::RenderTarget, rhi::RHIResourceState::Present));
}

void TestSceneColorTransition()
{
    render::RenderGraph graph;

    rhi::RHITextureDesc backBufferDesc{};
    backBufferDesc.width = 1280;
    backBufferDesc.height = 720;
    backBufferDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    backBufferDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::Present;
    FakeTexture backBuffer(backBufferDesc);

    const render::TextureHandle backBufferHandle =
        graph.ImportTexture(&backBuffer, rhi::RHIResourceState::Present, rhi::RHIResourceState::Present, "BackBuffer");

    rhi::RHITextureDesc sceneColorDesc{};
    sceneColorDesc.width = 1280;
    sceneColorDesc.height = 720;
    sceneColorDesc.format = rhi::RHIFormat::RGBA16_FLOAT;
    sceneColorDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::ShaderResource;
    sceneColorDesc.debugName = "SceneColor";

    const render::TextureHandle sceneColorHandle = graph.CreateTransientTexture(sceneColorDesc);

    TestPass forwardPass("Forward", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(sceneColorHandle, rhi::RHIResourceState::RenderTarget);
    });

    TestPass toneMapPass("ToneMap", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.ReadTexture(sceneColorHandle, rhi::RHIResourceState::ShaderResource);
        builder.WriteTexture(backBufferHandle, rhi::RHIResourceState::RenderTarget);
    });

    graph.AddPass(forwardPass);
    graph.AddPass(toneMapPass);
    graph.Compile();

    const render::CompiledRenderGraph& compiledGraph = graph.GetCompiledGraph();
    assert(compiledGraph.passes.size() == 2);
    assert(HasTransition(compiledGraph.passes[1].preBarriers, sceneColorHandle.index,
                         rhi::RHIResourceState::RenderTarget, rhi::RHIResourceState::ShaderResource));
}

void TestAliasingPlan()
{
    render::RenderGraph graph;

    rhi::RHITextureDesc sinkDesc{};
    sinkDesc.width = 64;
    sinkDesc.height = 64;
    sinkDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    sinkDesc.usage = rhi::RHITextureUsage::RenderTarget;
    FakeTexture sinkA(sinkDesc);
    FakeTexture sinkB(sinkDesc);

    const render::TextureHandle sinkAHandle =
        graph.ImportTexture(&sinkA, rhi::RHIResourceState::RenderTarget, rhi::RHIResourceState::RenderTarget, "SinkA");
    const render::TextureHandle sinkBHandle =
        graph.ImportTexture(&sinkB, rhi::RHIResourceState::RenderTarget, rhi::RHIResourceState::RenderTarget, "SinkB");

    rhi::RHITextureDesc transientDesc{};
    transientDesc.width = 1024;
    transientDesc.height = 1024;
    transientDesc.format = rhi::RHIFormat::RGBA16_FLOAT;
    transientDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::ShaderResource;

    const render::TextureHandle textureA = graph.CreateTransientTexture(transientDesc);
    const render::TextureHandle textureB = graph.CreateTransientTexture(transientDesc);

    TestPass passA("PassA", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(textureA, rhi::RHIResourceState::RenderTarget);
    });

    TestPass passARead("PassARead", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.ReadTexture(textureA, rhi::RHIResourceState::ShaderResource);
        builder.WriteTexture(sinkAHandle, rhi::RHIResourceState::RenderTarget);
    });

    TestPass passB("PassB", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(textureB, rhi::RHIResourceState::RenderTarget);
        builder.WriteTexture(sinkBHandle, rhi::RHIResourceState::RenderTarget);
    });

    graph.AddPass(passA);
    graph.AddPass(passARead);
    graph.AddPass(passB);
    graph.Compile();

    const render::CompiledRenderGraph& compiledGraph = graph.GetCompiledGraph();
    const auto& resources = compiledGraph.resources;
    assert(resources[textureA.index].alias.slot == resources[textureB.index].alias.slot);
    assert(resources[textureB.index].alias.previousResourceIndex == textureA.index);
    assert(HasAliasingBarrier(compiledGraph.passes[2].preBarriers, textureA.index, textureB.index));
    assert(HasAliasingBarrierWithStates(compiledGraph.passes[2].preBarriers,
                                        textureA.index,
                                        textureB.index,
                                        rhi::RHIResourceState::ShaderResource,
                                        rhi::RHIResourceState::RenderTarget,
                                        rhi::RHIPipelineStage::AllGraphics,
                                        rhi::RHIPipelineStage::ColorAttachmentOutput));
}

void TestAliasingRejectsUsageMismatch()
{
    render::RenderGraph graph;

    rhi::RHITextureDesc sinkDesc{};
    sinkDesc.width = 64;
    sinkDesc.height = 64;
    sinkDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    sinkDesc.usage = rhi::RHITextureUsage::RenderTarget;
    FakeTexture sinkA(sinkDesc);
    FakeTexture sinkB(sinkDesc);

    const render::TextureHandle sinkAHandle =
        graph.ImportTexture(&sinkA, rhi::RHIResourceState::RenderTarget, rhi::RHIResourceState::RenderTarget, "SinkA");
    const render::TextureHandle sinkBHandle =
        graph.ImportTexture(&sinkB, rhi::RHIResourceState::RenderTarget, rhi::RHIResourceState::RenderTarget, "SinkB");

    rhi::RHITextureDesc renderTargetDesc{};
    renderTargetDesc.width = 1024;
    renderTargetDesc.height = 1024;
    renderTargetDesc.format = rhi::RHIFormat::RGBA16_FLOAT;
    renderTargetDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::ShaderResource;

    rhi::RHITextureDesc uavDesc = renderTargetDesc;
    uavDesc.usage = rhi::RHITextureUsage::UnorderedAccess | rhi::RHITextureUsage::ShaderResource;

    const render::TextureHandle textureA = graph.CreateTransientTexture(renderTargetDesc);
    const render::TextureHandle textureB = graph.CreateTransientTexture(uavDesc);

    TestPass passA("PassA", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(textureA, rhi::RHIResourceState::RenderTarget);
    });

    TestPass passARead("PassARead", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.ReadTexture(textureA, rhi::RHIResourceState::ShaderResource);
        builder.WriteTexture(sinkAHandle, rhi::RHIResourceState::RenderTarget);
    });

    TestPass passB("PassB", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.ReadWriteTexture(textureB, rhi::RHIResourceState::UnorderedAccess);
        builder.WriteTexture(sinkBHandle, rhi::RHIResourceState::RenderTarget);
    });

    graph.AddPass(passA);
    graph.AddPass(passARead);
    graph.AddPass(passB);
    graph.Compile();

    const render::CompiledRenderGraph& compiledGraph = graph.GetCompiledGraph();
    const auto& resources = compiledGraph.resources;
    assert(resources[textureA.index].alias.slot != resources[textureB.index].alias.slot);
    assert(resources[textureB.index].alias.previousResourceIndex == render::kInvalidRenderGraphIndex);
}

void TestCrossQueueWaits()
{
    render::RenderGraph graph;

    rhi::RHITextureDesc sinkDesc{};
    sinkDesc.width = 64;
    sinkDesc.height = 64;
    sinkDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    sinkDesc.usage = rhi::RHITextureUsage::RenderTarget;
    FakeTexture sink(sinkDesc);
    const render::TextureHandle sinkHandle =
        graph.ImportTexture(&sink, rhi::RHIResourceState::RenderTarget, rhi::RHIResourceState::RenderTarget, "Sink");

    rhi::RHIBufferDesc bufferDesc{};
    bufferDesc.sizeBytes = 4096;
    bufferDesc.usage = rhi::RHIBufferUsage::StorageBuffer | rhi::RHIBufferUsage::CopyDest;
    bufferDesc.debugName = "CrossQueueBuffer";

    const render::BufferHandle bufferHandle = graph.CreateTransientBuffer(bufferDesc);

    TestPass copyPass("CopyPass", rhi::RHIQueueType::Copy, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteBuffer(bufferHandle, rhi::RHIResourceState::CopyDest);
    });

    TestPass graphicsPass("GraphicsPass", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.ReadBuffer(bufferHandle, rhi::RHIResourceState::ShaderResource);
        builder.WriteTexture(sinkHandle, rhi::RHIResourceState::RenderTarget);
    });

    graph.AddPass(copyPass);
    graph.AddPass(graphicsPass);
    graph.Compile();

    const render::CompiledRenderGraph& compiledGraph = graph.GetCompiledGraph();
    assert(compiledGraph.queueBatches.size() == 2);
    assert(compiledGraph.queueBatches[1].waitBatchIndices.size() == 1);
    assert(compiledGraph.queueBatches[1].waitBatchIndices[0] == 0);
}

void TestStageAwareBarriersAndWaitStages()
{
    render::RenderGraph graph;

    rhi::RHITextureDesc sinkDesc{};
    sinkDesc.width = 64;
    sinkDesc.height = 64;
    sinkDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    sinkDesc.usage = rhi::RHITextureUsage::RenderTarget;
    FakeTexture sink(sinkDesc);
    const render::TextureHandle sinkHandle =
        graph.ImportTexture(&sink, rhi::RHIResourceState::RenderTarget,
                            rhi::RHIResourceState::RenderTarget, "Sink");

    rhi::RHIBufferDesc bufferDesc{};
    bufferDesc.sizeBytes = 4096;
    bufferDesc.usage = rhi::RHIBufferUsage::StorageBuffer | rhi::RHIBufferUsage::CopyDest;
    bufferDesc.debugName = "StageAwareBuffer";

    const render::BufferHandle bufferHandle = graph.CreateTransientBuffer(bufferDesc);

    TestPass copyPass("CopyPass", rhi::RHIQueueType::Copy, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteBuffer(bufferHandle, rhi::RHIResourceState::CopyDest, rhi::RHIPipelineStage::Copy);
    });

    TestPass graphicsPass("GraphicsPass", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.ReadBuffer(bufferHandle, rhi::RHIResourceState::ShaderResource,
                           rhi::RHIPipelineStage::PixelShader);
        builder.WriteTexture(sinkHandle, rhi::RHIResourceState::RenderTarget,
                             rhi::RHIPipelineStage::ColorAttachmentOutput);
    });

    graph.AddPass(copyPass);
    graph.AddPass(graphicsPass);
    graph.Compile();

    const render::CompiledRenderGraph& compiledGraph = graph.GetCompiledGraph();
    assert(compiledGraph.queueBatches.size() == 2);
    assert(compiledGraph.queueBatches[1].waitBatchIndices.size() == 1);
    assert(compiledGraph.queueBatches[1].waitBatchIndices[0] == 0);
    assert(rhi::HasFlag(compiledGraph.queueBatches[1].waitStageMask, rhi::RHIPipelineStage::PixelShader));
    assert(!rhi::HasFlag(compiledGraph.queueBatches[1].waitStageMask, rhi::RHIPipelineStage::AllCommands));
    assert(HasTransitionWithStages(compiledGraph.passes[1].preBarriers, bufferHandle.index,
                                   rhi::RHIResourceState::CopyDest, rhi::RHIResourceState::ShaderResource,
                                   rhi::RHIPipelineStage::Copy, rhi::RHIPipelineStage::PixelShader));
}

void TestFinalBarrierStaysOnLastUsingBatch()
{
    render::RenderGraph graph;

    rhi::RHITextureDesc sinkDesc{};
    sinkDesc.width = 64;
    sinkDesc.height = 64;
    sinkDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    sinkDesc.usage = rhi::RHITextureUsage::RenderTarget;
    FakeTexture sink(sinkDesc);
    const render::TextureHandle sinkHandle =
        graph.ImportTexture(&sink, rhi::RHIResourceState::RenderTarget, rhi::RHIResourceState::RenderTarget, "Sink");

    rhi::RHITextureDesc backBufferDesc{};
    backBufferDesc.width = 1280;
    backBufferDesc.height = 720;
    backBufferDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    backBufferDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::Present;
    FakeTexture backBuffer(backBufferDesc);

    const render::TextureHandle backBufferHandle =
        graph.ImportTexture(&backBuffer, rhi::RHIResourceState::Present, rhi::RHIResourceState::Present, "BackBuffer");

    rhi::RHIBufferDesc scratchDesc{};
    scratchDesc.sizeBytes = 1024;
    scratchDesc.usage = rhi::RHIBufferUsage::StorageBuffer;
    const render::BufferHandle scratchBuffer = graph.CreateTransientBuffer(scratchDesc);

    TestPass graphicsPass("GraphicsPass", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(backBufferHandle, rhi::RHIResourceState::RenderTarget);
    });

    TestPass computePass("ComputePass", rhi::RHIQueueType::Compute, [&](render::RenderGraphBuilder& builder)
    {
        builder.ReadWriteBuffer(scratchBuffer, rhi::RHIResourceState::UnorderedAccess);
        builder.WriteTexture(sinkHandle, rhi::RHIResourceState::RenderTarget);
    });

    graph.AddPass(graphicsPass);
    graph.AddPass(computePass);
    graph.Compile();

    const render::CompiledRenderGraph& compiledGraph = graph.GetCompiledGraph();
    assert(compiledGraph.queueBatches.size() == 2);
    assert(HasTransition(compiledGraph.queueBatches[0].postBarriers, backBufferHandle.index,
                         rhi::RHIResourceState::RenderTarget, rhi::RHIResourceState::Present));
    assert(!HasTransition(compiledGraph.queueBatches[1].postBarriers, backBufferHandle.index,
                          rhi::RHIResourceState::RenderTarget, rhi::RHIResourceState::Present));
    assert(compiledGraph.queueBatches[1].waitBatchIndices.size() == 1);
    assert(compiledGraph.queueBatches[1].waitBatchIndices[0] == 0);
}

void TestDeadPassCullingRemovesUnusedBranch()
{
    render::RenderGraph graph;

    rhi::RHITextureDesc backBufferDesc{};
    backBufferDesc.width = 1280;
    backBufferDesc.height = 720;
    backBufferDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    backBufferDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::Present;
    FakeTexture backBuffer(backBufferDesc);

    const render::TextureHandle backBufferHandle =
        graph.ImportTexture(&backBuffer, rhi::RHIResourceState::Present, rhi::RHIResourceState::Present, "BackBuffer");

    rhi::RHITextureDesc scratchDesc{};
    scratchDesc.width = 256;
    scratchDesc.height = 256;
    scratchDesc.format = rhi::RHIFormat::RGBA16_FLOAT;
    scratchDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::ShaderResource;

    const render::TextureHandle deadTexture = graph.CreateTransientTexture(scratchDesc);

    TestPass deadPass("DeadPass", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(deadTexture, rhi::RHIResourceState::RenderTarget);
    });

    TestPass presentPass("PresentPass", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(backBufferHandle, rhi::RHIResourceState::RenderTarget);
    });

    graph.AddPass(deadPass);
    graph.AddPass(presentPass);
    graph.Compile();

    const render::CompiledRenderGraph& compiledGraph = graph.GetCompiledGraph();
    assert(compiledGraph.passes.size() == 1);
    assert(compiledGraph.passes[0].originalPassIndex == 1);
    assert(!compiledGraph.resources[deadTexture.index].lifetime.IsValid());
}

void TestDeadPassCullingKeepsSideEffectPass()
{
    render::RenderGraph graph;

    rhi::RHIBufferDesc scratchDesc{};
    scratchDesc.sizeBytes = 1024;
    scratchDesc.usage = rhi::RHIBufferUsage::StorageBuffer;
    const render::BufferHandle scratchBuffer = graph.CreateTransientBuffer(scratchDesc);

    TestPass sideEffectPass("GPUCapturePass", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteBuffer(scratchBuffer, rhi::RHIResourceState::UnorderedAccess);
    }, true);

    graph.AddPass(sideEffectPass);
    graph.Compile();

    const render::CompiledRenderGraph& compiledGraph = graph.GetCompiledGraph();
    assert(compiledGraph.passes.size() == 1);
    assert(compiledGraph.passes[0].originalPassIndex == 0);
    assert(compiledGraph.resources[scratchBuffer.index].lifetime.IsValid());
}

void TestUAVBarrier()
{
    render::RenderGraph graph;

    rhi::RHITextureDesc sinkDesc{};
    sinkDesc.width = 64;
    sinkDesc.height = 64;
    sinkDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    sinkDesc.usage = rhi::RHITextureUsage::RenderTarget;
    FakeTexture sink(sinkDesc);
    const render::TextureHandle sinkHandle =
        graph.ImportTexture(&sink, rhi::RHIResourceState::RenderTarget, rhi::RHIResourceState::RenderTarget, "Sink");

    rhi::RHIBufferDesc bufferDesc{};
    bufferDesc.sizeBytes = 1024;
    bufferDesc.usage = rhi::RHIBufferUsage::StorageBuffer;
    bufferDesc.debugName = "UAVBuffer";

    const render::BufferHandle bufferHandle = graph.CreateTransientBuffer(bufferDesc);

    TestPass firstWrite("FirstWrite", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteBuffer(bufferHandle, rhi::RHIResourceState::UnorderedAccess);
    });

    TestPass secondWrite("SecondWrite", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteBuffer(bufferHandle, rhi::RHIResourceState::UnorderedAccess);
        builder.WriteTexture(sinkHandle, rhi::RHIResourceState::RenderTarget);
    });

    graph.AddPass(firstWrite);
    graph.AddPass(secondWrite);
    graph.Compile();

    const render::CompiledRenderGraph& compiledGraph = graph.GetCompiledGraph();
    assert(HasUAVBarrier(compiledGraph.passes[1].preBarriers, bufferHandle.index));
}

void TestDeadPassCullingSkipsUnusedTransientAllocation()
{
    FakeDevice device;
    render::TransientResourcePool pool;
    render::RenderGraph graph;

    rhi::RHITextureDesc backBufferDesc{};
    backBufferDesc.width = 1280;
    backBufferDesc.height = 720;
    backBufferDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    backBufferDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::Present;
    FakeTexture backBuffer(backBufferDesc);

    const render::TextureHandle backBufferHandle =
        graph.ImportTexture(&backBuffer, rhi::RHIResourceState::Present, rhi::RHIResourceState::Present, "BackBuffer");

    rhi::RHITextureDesc deadDesc{};
    deadDesc.width = 512;
    deadDesc.height = 512;
    deadDesc.format = rhi::RHIFormat::RGBA16_FLOAT;
    deadDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::ShaderResource;
    const render::TextureHandle deadTexture = graph.CreateTransientTexture(deadDesc);

    TestPass deadPass("DeadPass", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(deadTexture, rhi::RHIResourceState::RenderTarget);
    });

    TestPass presentPass("PresentPass", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(backBufferHandle, rhi::RHIResourceState::RenderTarget);
    });

    graph.AddPass(deadPass);
    graph.AddPass(presentPass);
    graph.Compile();

    pool.Prepare(device, graph.GetCompiledGraph());
    assert(device.transientTextureCreateCount == 0);
}

void TestExecuteBatchesBarrierSubmission()
{
    ExecuteFakeDevice device;
    FakeFence fence;
    render::TransientResourcePool pool;
    render::RenderGraph graph;

    rhi::RHITextureDesc colorADesc{};
    colorADesc.width = 640;
    colorADesc.height = 480;
    colorADesc.format = rhi::RHIFormat::RGBA16_FLOAT;
    colorADesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::ShaderResource;
    FakeTexture colorA(colorADesc);

    rhi::RHITextureDesc colorBDesc = colorADesc;
    colorBDesc.debugName = "ColorB";
    FakeTexture colorB(colorBDesc);

    const render::TextureHandle colorAHandle =
        graph.ImportTexture(&colorA, rhi::RHIResourceState::ShaderResource,
                            rhi::RHIResourceState::ShaderResource, "ColorA");
    const render::TextureHandle colorBHandle =
        graph.ImportTexture(&colorB, rhi::RHIResourceState::ShaderResource,
                            rhi::RHIResourceState::ShaderResource, "ColorB");

    TestPass pass("Composite", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(colorAHandle, rhi::RHIResourceState::RenderTarget);
        builder.WriteTexture(colorBHandle, rhi::RHIResourceState::RenderTarget);
    });

    graph.AddPass(pass);

    render::RenderGraph::ExecuteDesc executeDesc{
        .device = device,
        .timelineFence = fence,
        .transientResourcePool = pool,
    };

    const uint64_t signalValue = graph.Execute(executeDesc);

    assert(device.lastCommandList != nullptr);
    assert(signalValue == 1);
    assert(device.lastCommandList->singleBarrierCalls == 0);
    assert(device.lastCommandList->barrierBatchSizes.size() == 2);
    assert(device.lastCommandList->barrierBatchSizes[0] == 2);
    assert(device.lastCommandList->barrierBatchSizes[1] == 2);
    assert(device.graphicsQueue.submitCount == 1);
    assert(device.graphicsQueue.lastSubmitCommandListCount == 1);
}

void TestExecuteUsesConsumingBatchWaitStage()
{
    ExecuteFakeDevice device;
    FakeFence fence;
    render::TransientResourcePool pool;
    render::RenderGraph graph;

    rhi::RHITextureDesc sinkDesc{};
    sinkDesc.width = 64;
    sinkDesc.height = 64;
    sinkDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    sinkDesc.usage = rhi::RHITextureUsage::RenderTarget;
    FakeTexture sink(sinkDesc);
    const render::TextureHandle sinkHandle =
        graph.ImportTexture(&sink, rhi::RHIResourceState::RenderTarget,
                            rhi::RHIResourceState::RenderTarget, "Sink");

    rhi::RHIBufferDesc bufferDesc{};
    bufferDesc.sizeBytes = 1024;
    bufferDesc.usage = rhi::RHIBufferUsage::StorageBuffer | rhi::RHIBufferUsage::CopyDest;
    const render::BufferHandle bufferHandle = graph.CreateTransientBuffer(bufferDesc);

    TestPass copyPass("CopyPass", rhi::RHIQueueType::Copy, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteBuffer(bufferHandle, rhi::RHIResourceState::CopyDest, rhi::RHIPipelineStage::Copy);
    });

    TestPass graphicsPass("GraphicsPass", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.ReadBuffer(bufferHandle, rhi::RHIResourceState::ShaderResource,
                           rhi::RHIPipelineStage::PixelShader);
        builder.WriteTexture(sinkHandle, rhi::RHIResourceState::RenderTarget,
                             rhi::RHIPipelineStage::ColorAttachmentOutput);
    });

    graph.AddPass(copyPass);
    graph.AddPass(graphicsPass);

    render::RenderGraph::ExecuteDesc executeDesc{
        .device = device,
        .timelineFence = fence,
        .transientResourcePool = pool,
    };

    const uint64_t signalValue = graph.Execute(executeDesc);

    assert(signalValue == 2);
    assert(device.copyQueue.submitCount == 1);
    assert(device.graphicsQueue.submitCount == 1);
    assert(device.graphicsQueue.lastTimelineWaitStages.size() == 1);
    assert(rhi::HasFlag(device.graphicsQueue.lastTimelineWaitStages[0], rhi::RHIPipelineStage::PixelShader));
    assert(!rhi::HasFlag(device.graphicsQueue.lastTimelineWaitStages[0], rhi::RHIPipelineStage::AllCommands));
}

void TestExecuteRecordsTimestampProfiling()
{
    ExecuteFakeDevice device;
    FakeFence fence;
    render::TransientResourcePool pool;
    render::RenderGraph graph;

    rhi::RHITextureDesc backBufferDesc{};
    backBufferDesc.width = 320;
    backBufferDesc.height = 180;
    backBufferDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    backBufferDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::Present;
    FakeTexture backBuffer(backBufferDesc);

    const render::TextureHandle backBufferHandle =
        graph.ImportTexture(&backBuffer, rhi::RHIResourceState::Present, rhi::RHIResourceState::Present, "BackBuffer");

    TestPass pass("TimestampedPass", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(backBufferHandle, rhi::RHIResourceState::RenderTarget);
    });

    graph.AddPass(pass);
    graph.Compile();

    FakeTimestampQueryPool graphicsQueries(8, rhi::RHIQueueType::Graphics);
    std::array<rhi::IRHITimestampQueryPool*, 3> queryPools{};
    std::array<uint32_t, 3> queryCounts{};
    std::vector<render::RenderGraphTimestampPassRange> passRanges(graph.GetCompiledGraph().passes.size());
    queryPools[0] = &graphicsQueries;

    render::RenderGraph::ExecuteDesc executeDesc{
        .device = device,
        .timelineFence = fence,
        .transientResourcePool = pool,
        .timestampProfiling = render::RenderGraphTimestampProfilingDesc{
            .queryPools = std::span<rhi::IRHITimestampQueryPool*>(queryPools.data(), queryPools.size()),
            .queryCounts = std::span<uint32_t>(queryCounts.data(), queryCounts.size()),
            .passRanges = std::span<render::RenderGraphTimestampPassRange>(passRanges.data(), passRanges.size()),
        },
    };

    const uint64_t signalValue = graph.Execute(executeDesc);

    assert(device.lastCommandList != nullptr);
    assert(signalValue == 1);
    assert(device.lastCommandList->timestampResetCalls == 1);
    assert(device.lastCommandList->lastResetFirstQuery == 0);
    assert(device.lastCommandList->lastResetQueryCount == graphicsQueries.GetDesc().queryCount);
    assert(device.lastCommandList->timestampWriteCalls == 2);
    assert(device.lastCommandList->writtenTimestampIndices.size() == 2);
    assert(device.lastCommandList->writtenTimestampIndices[0] == 0);
    assert(device.lastCommandList->writtenTimestampIndices[1] == 1);
    assert(device.lastCommandList->timestampResolveCalls == 1);
    assert(device.lastCommandList->lastResolveFirstQuery == 0);
    assert(device.lastCommandList->lastResolveQueryCount == 2);
    assert(queryCounts[0] == 2);
    assert(passRanges[0].valid);
    assert(passRanges[0].beginQueryIndex == 0);
    assert(passRanges[0].endQueryIndex == 1);
}

void TestTransientPoolRecreatesOnAliasSlotChange()
{
    FakeDevice device;
    render::TransientResourcePool pool;
    render::CompiledRenderGraph compiledGraph;

    compiledGraph.resources.resize(1);
    render::RenderGraphResourceInfo& resource = compiledGraph.resources[0];
    resource.kind = render::ResourceKind::Texture;
    resource.imported = false;
    resource.textureDesc.width = 512;
    resource.textureDesc.height = 512;
    resource.textureDesc.format = rhi::RHIFormat::RGBA16_FLOAT;
    resource.textureDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::ShaderResource;
    resource.lifetime.firstUsePass = 0;
    resource.lifetime.lastUsePass = 0;
    resource.alias.slot = 0;

    pool.Prepare(device, compiledGraph);
    assert(device.transientTextureCreateCount == 1);
    assert(device.observedAliasSlots.back() == 0);

    pool.Prepare(device, compiledGraph);
    assert(device.transientTextureCreateCount == 1);

    compiledGraph.resources[0].alias.slot = 1;
    pool.Prepare(device, compiledGraph);
    assert(device.transientTextureCreateCount == 2);
    assert(device.observedAliasSlots.back() == 1);
}

void TestCommandListPoolReusesCompletedLists()
{
    PoolFakeDevice device;
    render::CommandListPool pool;

    render::CommandListPool::Lease first = pool.Acquire(device, rhi::RHIQueueType::Graphics, 0);
    assert(first.IsValid());
    assert(device.createCommandListCount == 1);
    pool.Release(first, 3);

    render::CommandListPool::Lease second = pool.Acquire(device, rhi::RHIQueueType::Graphics, 2);
    assert(second.IsValid());
    assert(device.createCommandListCount == 2);
    pool.Release(second, 4);

    render::CommandListPool::Lease third = pool.Acquire(device, rhi::RHIQueueType::Graphics, 3);
    assert(third.IsValid());
    assert(device.createCommandListCount == 2);
    pool.Release(third, 5);
}

} // namespace

int main()
{
    TestImportedBackBufferTransitions();
    TestImportedStateRefreshUpdatesCompiledBarriers();
    TestSceneColorTransition();
    TestAliasingPlan();
    TestAliasingRejectsUsageMismatch();
    TestCrossQueueWaits();
    TestStageAwareBarriersAndWaitStages();
    TestFinalBarrierStaysOnLastUsingBatch();
    TestDeadPassCullingRemovesUnusedBranch();
    TestDeadPassCullingKeepsSideEffectPass();
    TestUAVBarrier();
    TestDeadPassCullingSkipsUnusedTransientAllocation();
    TestExecuteBatchesBarrierSubmission();
    TestExecuteUsesConsumingBatchWaitStage();
    TestExecuteRecordsTimestampProfiling();
    TestTransientPoolRecreatesOnAliasSlotChange();
    TestCommandListPoolReusesCompletedLists();

    std::cout << "Render Graph compile tests passed\n";
    return 0;
}
