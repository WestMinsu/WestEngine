// =============================================================================
// WestEngine Tests - Render
// Render Graph compile behavior
// =============================================================================
#include "render/RenderGraph/RenderGraph.h"
#include "render/RenderGraph/RenderGraphCompiler.h"
#include "TestAssert.h"

#include <functional>
#include <iostream>

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

class TestPass final : public render::RenderGraphPass
{
public:
    using SetupFn = std::function<void(render::RenderGraphBuilder&)>;

    TestPass(const char* debugName, rhi::RHIQueueType queueType, SetupFn setup)
        : m_debugName(debugName)
        , m_queueType(queueType)
        , m_setup(std::move(setup))
    {
    }

    void Setup(render::RenderGraphBuilder& builder) override
    {
        m_setup(builder);
    }

    void Execute(render::RenderGraphContext&, rhi::IRHICommandList&) override
    {
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
    });

    TestPass passB("PassB", rhi::RHIQueueType::Graphics, [&](render::RenderGraphBuilder& builder)
    {
        builder.WriteTexture(textureB, rhi::RHIResourceState::RenderTarget);
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
}

void TestCrossQueueWaits()
{
    render::RenderGraph graph;

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
    });

    graph.AddPass(copyPass);
    graph.AddPass(graphicsPass);
    graph.Compile();

    const render::CompiledRenderGraph& compiledGraph = graph.GetCompiledGraph();
    assert(compiledGraph.queueBatches.size() == 2);
    assert(compiledGraph.queueBatches[1].waitBatchIndices.size() == 1);
    assert(compiledGraph.queueBatches[1].waitBatchIndices[0] == 0);
}

void TestUAVBarrier()
{
    render::RenderGraph graph;

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
    });

    graph.AddPass(firstWrite);
    graph.AddPass(secondWrite);
    graph.Compile();

    const render::CompiledRenderGraph& compiledGraph = graph.GetCompiledGraph();
    assert(HasUAVBarrier(compiledGraph.passes[1].preBarriers, bufferHandle.index));
}

} // namespace

int main()
{
    TestImportedBackBufferTransitions();
    TestSceneColorTransition();
    TestAliasingPlan();
    TestCrossQueueWaits();
    TestUAVBarrier();

    std::cout << "Render Graph compile tests passed\n";
    return 0;
}
