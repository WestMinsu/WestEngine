// =============================================================================
// WestEngine Tests - Shader
// PSO cache stable hash behavior
// =============================================================================
#include "shader/PSOCache.h"
#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHIFence.h"
#include "rhi/interface/IRHIPipeline.h"
#include "rhi/interface/IRHIQueue.h"
#include "rhi/interface/IRHISampler.h"
#include "rhi/interface/IRHISemaphore.h"
#include "rhi/interface/IRHISwapChain.h"
#include "rhi/interface/IRHITexture.h"
#include "TestAssert.h"

#include <array>
#include <atomic>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

using namespace west;

namespace
{

class FakePipeline final : public rhi::IRHIPipeline
{
public:
    FakePipeline(rhi::RHIPipelineType type, uint64_t psoHash)
        : m_type(type)
        , m_psoHash(psoHash)
    {
    }

    rhi::RHIPipelineType GetType() const override
    {
        return m_type;
    }

    uint64_t GetPSOHash() const override
    {
        return m_psoHash;
    }

private:
    rhi::RHIPipelineType m_type;
    uint64_t m_psoHash = 0;
};

class FakeDevice final : public rhi::IRHIDevice
{
public:
    std::unique_ptr<rhi::IRHIBuffer> CreateBuffer(const rhi::RHIBufferDesc&) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHITexture> CreateTexture(const rhi::RHITextureDesc&) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHIBuffer> CreateTransientBuffer(const rhi::RHIBufferDesc&, uint32_t) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHITexture> CreateTransientTexture(const rhi::RHITextureDesc&, uint32_t) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHISampler> CreateSampler(const rhi::RHISamplerDesc&) override
    {
        return nullptr;
    }

    std::unique_ptr<rhi::IRHIPipeline> CreateGraphicsPipeline(const rhi::RHIGraphicsPipelineDesc& desc) override
    {
        ++graphicsCreateCount;
        return std::make_unique<FakePipeline>(rhi::RHIPipelineType::Graphics, desc.psoHash);
    }

    std::unique_ptr<rhi::IRHIPipeline> CreateComputePipeline(const rhi::RHIComputePipelineDesc& desc) override
    {
        ++computeCreateCount;
        return std::make_unique<FakePipeline>(rhi::RHIPipelineType::Compute, desc.psoHash);
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
        currentFrameFenceValue = fenceValue;
    }

    uint64_t GetCurrentFrameFenceValue() const override
    {
        return currentFrameFenceValue;
    }

    std::atomic<int> graphicsCreateCount{0};
    std::atomic<int> computeCreateCount{0};
    uint64_t currentFrameFenceValue = 0;
};

} // namespace

int main()
{
    std::array<uint8_t, 4> vsBytes = {1, 2, 3, 4};
    std::array<uint8_t, 4> psBytes = {5, 6, 7, 8};
    std::array<uint8_t, 4> psBytesChanged = {5, 6, 7, 9};

    rhi::RHIVertexAttribute vertexAttributes[] = {
        {"POSITION", rhi::RHIFormat::RGB32_FLOAT, 0},
        {"TEXCOORD", rhi::RHIFormat::RG32_FLOAT, 12},
    };
    rhi::RHIFormat colorFormat = rhi::RHIFormat::BGRA8_UNORM;

    rhi::RHIGraphicsPipelineDesc graphicsDesc{};
    graphicsDesc.vertexShader = vsBytes;
    graphicsDesc.fragmentShader = psBytes;
    graphicsDesc.vertexAttributes = vertexAttributes;
    graphicsDesc.vertexStride = 20;
    graphicsDesc.cullMode = rhi::RHICullMode::None;
    graphicsDesc.depthTest = false;
    graphicsDesc.depthWrite = false;
    graphicsDesc.colorFormats = {&colorFormat, 1};
    graphicsDesc.depthFormat = rhi::RHIFormat::Unknown;
    graphicsDesc.pushConstantSizeBytes = 16;
    graphicsDesc.debugName = "A";

    rhi::RHIGraphicsPipelineDesc sameGraphicsDesc = graphicsDesc;
    sameGraphicsDesc.debugName = "B";
    sameGraphicsDesc.psoHash = 0x1234;

    const uint64_t graphicsHash = shader::PSOCache::ComputeGraphicsPipelineHash(graphicsDesc);
    const uint64_t sameGraphicsHash = shader::PSOCache::ComputeGraphicsPipelineHash(sameGraphicsDesc);
    assert(graphicsHash == sameGraphicsHash);

    rhi::RHIGraphicsPipelineDesc shaderChangedDesc = graphicsDesc;
    shaderChangedDesc.fragmentShader = psBytesChanged;
    assert(graphicsHash != shader::PSOCache::ComputeGraphicsPipelineHash(shaderChangedDesc));

    rhi::RHIGraphicsPipelineDesc stateChangedDesc = graphicsDesc;
    stateChangedDesc.cullMode = rhi::RHICullMode::Back;
    assert(graphicsHash != shader::PSOCache::ComputeGraphicsPipelineHash(stateChangedDesc));

    std::array<uint8_t, 4> csBytes = {9, 10, 11, 12};
    std::array<uint8_t, 4> csBytesChanged = {9, 10, 11, 13};

    rhi::RHIComputePipelineDesc computeDesc{};
    computeDesc.computeShader = csBytes;
    computeDesc.pushConstantSizeBytes = 0;
    computeDesc.debugName = "ComputeA";

    rhi::RHIComputePipelineDesc sameComputeDesc = computeDesc;
    sameComputeDesc.debugName = "ComputeB";
    sameComputeDesc.psoHash = 0x5678;

    const uint64_t computeHash = shader::PSOCache::ComputeComputePipelineHash(computeDesc);
    assert(computeHash == shader::PSOCache::ComputeComputePipelineHash(sameComputeDesc));

    rhi::RHIComputePipelineDesc computeShaderChangedDesc = computeDesc;
    computeShaderChangedDesc.computeShader = csBytesChanged;
    assert(computeHash != shader::PSOCache::ComputeComputePipelineHash(computeShaderChangedDesc));

    rhi::RHIComputePipelineDesc computeStateChangedDesc = computeDesc;
    computeStateChangedDesc.pushConstantSizeBytes = 16;
    assert(computeHash != shader::PSOCache::ComputeComputePipelineHash(computeStateChangedDesc));

    FakeDevice device;
    shader::PSOCache cache;
    rhi::IRHIPipeline* firstPipeline = cache.GetOrCreateGraphicsPipeline(device, graphicsDesc);
    rhi::IRHIPipeline* secondPipeline = cache.GetOrCreateGraphicsPipeline(device, sameGraphicsDesc);
    assert(firstPipeline != nullptr);
    assert(firstPipeline == secondPipeline);
    assert(firstPipeline->GetPSOHash() == graphicsHash);
    assert(device.graphicsCreateCount.load() == 1);

    rhi::IRHIPipeline* shaderChangedPipeline = cache.GetOrCreateGraphicsPipeline(device, shaderChangedDesc);
    rhi::IRHIPipeline* stateChangedPipeline = cache.GetOrCreateGraphicsPipeline(device, stateChangedDesc);
    assert(shaderChangedPipeline != nullptr);
    assert(stateChangedPipeline != nullptr);
    assert(shaderChangedPipeline != firstPipeline);
    assert(stateChangedPipeline != firstPipeline);
    assert(device.graphicsCreateCount.load() == 3);

    FakeDevice threadedDevice;
    shader::PSOCache threadedCache;
    constexpr size_t kThreadCount = 8;
    std::array<rhi::IRHIPipeline*, kThreadCount> threadedPipelines{};
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (size_t i = 0; i < kThreadCount; ++i)
    {
        threads.emplace_back([&threadedCache, &threadedDevice, &graphicsDesc, &threadedPipelines, i]()
        {
            threadedPipelines[i] = threadedCache.GetOrCreateGraphicsPipeline(threadedDevice, graphicsDesc);
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    assert(threadedDevice.graphicsCreateCount.load() == 1);
    for (rhi::IRHIPipeline* pipeline : threadedPipelines)
    {
        assert(pipeline != nullptr);
        assert(pipeline == threadedPipelines[0]);
    }

    std::cout << "PSOCache hash tests passed\n";
    return 0;
}
