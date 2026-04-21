// =============================================================================
// WestEngine - Render
// Render Graph frame builder, compiler, and executor
// =============================================================================
#pragma once

#include "core/Assert.h"
#include "render/RenderGraph/RenderGraphCompiler.h"
#include "render/RenderGraph/RenderGraphPass.h"
#include "render/RenderGraph/TransientResourcePool.h"

#include <span>
#include <vector>

namespace west::rhi
{
class IRHIBuffer;
class IRHIDevice;
class IRHIFence;
class IRHISemaphore;
class IRHITexture;
} // namespace west::rhi

namespace west::render
{

class RenderGraphBuilder
{
public:
    explicit RenderGraphBuilder(std::vector<ResourceUse>& uses)
        : m_uses(uses)
    {
    }

    void ReadTexture(TextureHandle handle, rhi::RHIResourceState state = rhi::RHIResourceState::ShaderResource);
    void WriteTexture(TextureHandle handle, rhi::RHIResourceState state = rhi::RHIResourceState::RenderTarget);
    void ReadWriteTexture(TextureHandle handle,
                          rhi::RHIResourceState state = rhi::RHIResourceState::UnorderedAccess);

    void ReadBuffer(BufferHandle handle, rhi::RHIResourceState state = rhi::RHIResourceState::ShaderResource);
    void WriteBuffer(BufferHandle handle, rhi::RHIResourceState state = rhi::RHIResourceState::CopyDest);
    void ReadWriteBuffer(BufferHandle handle,
                         rhi::RHIResourceState state = rhi::RHIResourceState::UnorderedAccess);

private:
    void AddUse(ResourceKind resourceKind, uint32_t resourceIndex, rhi::RHIResourceState state,
                ResourceAccessType accessType);

    std::vector<ResourceUse>& m_uses;
};

class RenderGraphContext
{
public:
    RenderGraphContext(rhi::IRHIDevice& device, const CompiledRenderGraph& compiledGraph,
                       std::span<rhi::IRHITexture* const> textures, std::span<rhi::IRHIBuffer* const> buffers)
        : m_device(device)
        , m_compiledGraph(compiledGraph)
        , m_textures(textures)
        , m_buffers(buffers)
    {
    }

    [[nodiscard]] rhi::IRHIDevice& GetDevice() const
    {
        return m_device;
    }

    [[nodiscard]] const CompiledRenderGraph& GetCompiledGraph() const
    {
        return m_compiledGraph;
    }

    [[nodiscard]] rhi::IRHITexture* GetTexture(TextureHandle handle) const;
    [[nodiscard]] rhi::IRHIBuffer* GetBuffer(BufferHandle handle) const;

private:
    rhi::IRHIDevice& m_device;
    const CompiledRenderGraph& m_compiledGraph;
    std::span<rhi::IRHITexture* const> m_textures;
    std::span<rhi::IRHIBuffer* const> m_buffers;
};

class RenderGraph
{
public:
    struct ExecuteDesc
    {
        rhi::IRHIDevice& device;
        rhi::IRHIFence& timelineFence;
        TransientResourcePool& transientResourcePool;
        rhi::IRHISemaphore* waitSemaphore = nullptr;
        rhi::IRHISemaphore* signalSemaphore = nullptr;
    };

    TextureHandle ImportTexture(rhi::IRHITexture* texture, rhi::RHIResourceState initialState,
                                rhi::RHIResourceState finalState, const char* debugName = nullptr);
    BufferHandle ImportBuffer(rhi::IRHIBuffer* buffer, rhi::RHIResourceState initialState,
                              rhi::RHIResourceState finalState, const char* debugName = nullptr);

    TextureHandle CreateTransientTexture(const rhi::RHITextureDesc& desc);
    BufferHandle CreateTransientBuffer(const rhi::RHIBufferDesc& desc);

    void AddPass(RenderGraphPass& pass);
    void Compile();
    [[nodiscard]] uint64_t Execute(const ExecuteDesc& desc);
    void Reset();

    [[nodiscard]] const CompiledRenderGraph& GetCompiledGraph() const
    {
        WEST_ASSERT(m_isCompiled);
        return m_compiledGraph;
    }

private:
    std::vector<RenderGraphResourceInfo> m_resources;
    std::vector<RenderGraphPassNode> m_passNodes;
    CompiledRenderGraph m_compiledGraph;
    bool m_isCompiled = false;
};

} // namespace west::render
