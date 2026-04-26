// =============================================================================
// WestEngine - Render
// Render Graph frame builder and executor
// =============================================================================
#include "render/RenderGraph/RenderGraph.h"

#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHIFence.h"
#include "rhi/interface/IRHIQueue.h"
#include "rhi/interface/IRHISemaphore.h"
#include "rhi/interface/IRHITimestampQueryPool.h"
#include "rhi/interface/IRHITexture.h"

#include <array>
#include <memory>
#include <string>

namespace west::render
{

namespace
{
[[nodiscard]] rhi::RHIBarrierDesc ResolveBarrier(const CompiledBarrier& barrier,
                                                 std::span<rhi::IRHITexture* const> textures,
                                                 std::span<rhi::IRHIBuffer* const> buffers)
{
    rhi::RHIBarrierDesc resolvedBarrier{};
    resolvedBarrier.type = barrier.type;
    resolvedBarrier.stateBefore = barrier.stateBefore;
    resolvedBarrier.stateAfter = barrier.stateAfter;
    resolvedBarrier.srcStageMask = barrier.srcStageMask;
    resolvedBarrier.dstStageMask = barrier.dstStageMask;

    if (barrier.type == rhi::RHIBarrierDesc::Type::Transition)
    {
        if (barrier.resourceKind == ResourceKind::Texture)
        {
            resolvedBarrier.texture = textures[barrier.resourceIndex];
        }
        else
        {
            resolvedBarrier.buffer = buffers[barrier.resourceIndex];
        }
    }
    else if (barrier.type == rhi::RHIBarrierDesc::Type::Aliasing)
    {
        resolvedBarrier.aliasBefore = barrier.aliasBeforeResourceIndex != kInvalidRenderGraphIndex
                                          ? textures[barrier.aliasBeforeResourceIndex]
                                          : nullptr;
        resolvedBarrier.aliasAfter = barrier.aliasAfterResourceIndex != kInvalidRenderGraphIndex
                                         ? textures[barrier.aliasAfterResourceIndex]
                                         : nullptr;
    }
    else if (barrier.type == rhi::RHIBarrierDesc::Type::UAV)
    {
        if (barrier.resourceKind == ResourceKind::Texture)
        {
            resolvedBarrier.texture = textures[barrier.resourceIndex];
        }
        else
        {
            resolvedBarrier.buffer = buffers[barrier.resourceIndex];
        }
    }

    return resolvedBarrier;
}

void RecordResolvedBarriers(rhi::IRHICommandList& commandList, std::span<const CompiledBarrier> compiledBarriers,
                            std::span<rhi::IRHITexture* const> textures,
                            std::span<rhi::IRHIBuffer* const> buffers,
                            std::vector<rhi::RHIBarrierDesc>& resolvedBarriers)
{
    if (compiledBarriers.empty())
    {
        return;
    }

    resolvedBarriers.clear();
    resolvedBarriers.reserve(compiledBarriers.size());
    for (const CompiledBarrier& barrier : compiledBarriers)
    {
        resolvedBarriers.push_back(ResolveBarrier(barrier, textures, buffers));
    }

    commandList.ResourceBarriers(std::span<const rhi::RHIBarrierDesc>(resolvedBarriers.data(), resolvedBarriers.size()));
}

[[nodiscard]] bool HasTimestampProfiling(const RenderGraphTimestampProfilingDesc& profiling)
{
    return !profiling.queryPools.empty() && !profiling.queryCounts.empty() && !profiling.passRanges.empty();
}

} // namespace

void RenderGraphBuilder::ReadTexture(TextureHandle handle, rhi::RHIResourceState state,
                                     rhi::RHIPipelineStage stageMask)
{
    AddUse(ResourceKind::Texture, handle.index, state, ResourceAccessType::Read, stageMask);
}

void RenderGraphBuilder::WriteTexture(TextureHandle handle, rhi::RHIResourceState state,
                                      rhi::RHIPipelineStage stageMask)
{
    AddUse(ResourceKind::Texture, handle.index, state, ResourceAccessType::Write, stageMask);
}

void RenderGraphBuilder::ReadWriteTexture(TextureHandle handle, rhi::RHIResourceState state,
                                          rhi::RHIPipelineStage stageMask)
{
    AddUse(ResourceKind::Texture, handle.index, state, ResourceAccessType::ReadWrite, stageMask);
}

void RenderGraphBuilder::ReadBuffer(BufferHandle handle, rhi::RHIResourceState state,
                                    rhi::RHIPipelineStage stageMask)
{
    AddUse(ResourceKind::Buffer, handle.index, state, ResourceAccessType::Read, stageMask);
}

void RenderGraphBuilder::WriteBuffer(BufferHandle handle, rhi::RHIResourceState state,
                                     rhi::RHIPipelineStage stageMask)
{
    AddUse(ResourceKind::Buffer, handle.index, state, ResourceAccessType::Write, stageMask);
}

void RenderGraphBuilder::ReadWriteBuffer(BufferHandle handle, rhi::RHIResourceState state,
                                         rhi::RHIPipelineStage stageMask)
{
    AddUse(ResourceKind::Buffer, handle.index, state, ResourceAccessType::ReadWrite, stageMask);
}

void RenderGraphBuilder::AddUse(ResourceKind resourceKind, uint32_t resourceIndex, rhi::RHIResourceState state,
                                ResourceAccessType accessType, rhi::RHIPipelineStage stageMask)
{
    WEST_ASSERT(resourceIndex != kInvalidRenderGraphIndex);
    m_uses.push_back(ResourceUse{
        .resourceKind = resourceKind,
        .resourceIndex = resourceIndex,
        .state = state,
        .accessType = accessType,
        .stageMask = stageMask,
    });
}

rhi::IRHITexture* RenderGraphContext::GetTexture(TextureHandle handle) const
{
    WEST_ASSERT(handle.IsValid());
    WEST_ASSERT(handle.index < m_textures.size());
    return m_textures[handle.index];
}

rhi::IRHIBuffer* RenderGraphContext::GetBuffer(BufferHandle handle) const
{
    WEST_ASSERT(handle.IsValid());
    WEST_ASSERT(handle.index < m_buffers.size());
    return m_buffers[handle.index];
}

TextureHandle RenderGraph::ImportTexture(rhi::IRHITexture* texture, rhi::RHIResourceState initialState,
                                         rhi::RHIResourceState finalState, const char* debugName)
{
    WEST_ASSERT(texture != nullptr);

    RenderGraphResourceInfo resource{};
    resource.kind = ResourceKind::Texture;
    resource.imported = true;
    resource.debugName = debugName != nullptr ? debugName : (texture->GetDesc().debugName != nullptr
                                                                 ? texture->GetDesc().debugName
                                                                 : "ImportedTexture");
    resource.textureDesc = texture->GetDesc();
    resource.importedTexture = texture;
    resource.initialState = initialState;
    resource.finalState = finalState;
    resource.estimatedSizeBytes = resource.textureDesc.width * resource.textureDesc.height;

    const uint32_t resourceIndex = static_cast<uint32_t>(m_resources.size());
    m_resources.push_back(resource);
    m_isCompiled = false;
    return TextureHandle{resourceIndex};
}

BufferHandle RenderGraph::ImportBuffer(rhi::IRHIBuffer* buffer, rhi::RHIResourceState initialState,
                                       rhi::RHIResourceState finalState, const char* debugName)
{
    WEST_ASSERT(buffer != nullptr);

    RenderGraphResourceInfo resource{};
    resource.kind = ResourceKind::Buffer;
    resource.imported = true;
    resource.debugName = debugName != nullptr ? debugName : (buffer->GetDesc().debugName != nullptr
                                                                 ? buffer->GetDesc().debugName
                                                                 : "ImportedBuffer");
    resource.bufferDesc = buffer->GetDesc();
    resource.importedBuffer = buffer;
    resource.initialState = initialState;
    resource.finalState = finalState;
    resource.estimatedSizeBytes = buffer->GetDesc().sizeBytes;

    const uint32_t resourceIndex = static_cast<uint32_t>(m_resources.size());
    m_resources.push_back(resource);
    m_isCompiled = false;
    return BufferHandle{resourceIndex};
}

void RenderGraph::UpdateImportedTexture(TextureHandle handle, rhi::IRHITexture* texture,
                                        rhi::RHIResourceState initialState, rhi::RHIResourceState finalState,
                                        const char* debugName)
{
    WEST_ASSERT(handle.IsValid());
    WEST_ASSERT(texture != nullptr);
    WEST_ASSERT(handle.index < m_resources.size());

    RenderGraphResourceInfo& resource = m_resources[handle.index];
    WEST_ASSERT(resource.kind == ResourceKind::Texture);
    WEST_ASSERT(resource.imported);

    const rhi::RHITextureDesc newDesc = texture->GetDesc();
    const bool descChanged = resource.textureDesc != newDesc;

    resource.textureDesc = newDesc;
    resource.importedTexture = texture;
    resource.initialState = initialState;
    resource.finalState = finalState;
    resource.debugName = debugName != nullptr ? debugName : (newDesc.debugName != nullptr ? newDesc.debugName : resource.debugName);
    resource.estimatedSizeBytes = static_cast<uint64_t>(newDesc.width) * newDesc.height;

    if (descChanged)
    {
        m_isCompiled = false;
        return;
    }

    if (m_isCompiled && handle.index < m_compiledGraph.resources.size())
    {
        RenderGraphResourceInfo& compiledResource = m_compiledGraph.resources[handle.index];
        compiledResource.textureDesc = resource.textureDesc;
        compiledResource.importedTexture = resource.importedTexture;
        compiledResource.initialState = resource.initialState;
        compiledResource.finalState = resource.finalState;
        compiledResource.debugName = resource.debugName;
        compiledResource.estimatedSizeBytes = resource.estimatedSizeBytes;
        RenderGraphCompiler::RefreshBarriers(m_compiledGraph);
    }
}

void RenderGraph::UpdateImportedBuffer(BufferHandle handle, rhi::IRHIBuffer* buffer,
                                       rhi::RHIResourceState initialState, rhi::RHIResourceState finalState,
                                       const char* debugName)
{
    WEST_ASSERT(handle.IsValid());
    WEST_ASSERT(buffer != nullptr);
    WEST_ASSERT(handle.index < m_resources.size());

    RenderGraphResourceInfo& resource = m_resources[handle.index];
    WEST_ASSERT(resource.kind == ResourceKind::Buffer);
    WEST_ASSERT(resource.imported);

    const rhi::RHIBufferDesc newDesc = buffer->GetDesc();
    const bool descChanged = resource.bufferDesc != newDesc;

    resource.bufferDesc = newDesc;
    resource.importedBuffer = buffer;
    resource.initialState = initialState;
    resource.finalState = finalState;
    resource.debugName = debugName != nullptr ? debugName : (newDesc.debugName != nullptr ? newDesc.debugName : resource.debugName);
    resource.estimatedSizeBytes = newDesc.sizeBytes;

    if (descChanged)
    {
        m_isCompiled = false;
        return;
    }

    if (m_isCompiled && handle.index < m_compiledGraph.resources.size())
    {
        RenderGraphResourceInfo& compiledResource = m_compiledGraph.resources[handle.index];
        compiledResource.bufferDesc = resource.bufferDesc;
        compiledResource.importedBuffer = resource.importedBuffer;
        compiledResource.initialState = resource.initialState;
        compiledResource.finalState = resource.finalState;
        compiledResource.debugName = resource.debugName;
        compiledResource.estimatedSizeBytes = resource.estimatedSizeBytes;
        RenderGraphCompiler::RefreshBarriers(m_compiledGraph);
    }
}

TextureHandle RenderGraph::CreateTransientTexture(const rhi::RHITextureDesc& desc)
{
    RenderGraphResourceInfo resource{};
    resource.kind = ResourceKind::Texture;
    resource.imported = false;
    resource.debugName = desc.debugName != nullptr ? desc.debugName : "TransientTexture";
    resource.textureDesc = desc;
    resource.initialState = rhi::RHIResourceState::Undefined;
    resource.finalState = rhi::RHIResourceState::Common;

    const uint32_t resourceIndex = static_cast<uint32_t>(m_resources.size());
    m_resources.push_back(resource);
    m_isCompiled = false;
    return TextureHandle{resourceIndex};
}

BufferHandle RenderGraph::CreateTransientBuffer(const rhi::RHIBufferDesc& desc)
{
    RenderGraphResourceInfo resource{};
    resource.kind = ResourceKind::Buffer;
    resource.imported = false;
    resource.debugName = desc.debugName != nullptr ? desc.debugName : "TransientBuffer";
    resource.bufferDesc = desc;
    resource.initialState = rhi::RHIResourceState::Undefined;
    resource.finalState = rhi::RHIResourceState::Common;

    const uint32_t resourceIndex = static_cast<uint32_t>(m_resources.size());
    m_resources.push_back(resource);
    m_isCompiled = false;
    return BufferHandle{resourceIndex};
}

void RenderGraph::AddPass(RenderGraphPass& pass)
{
    m_passNodes.push_back(RenderGraphPassNode{.pass = &pass});
    m_isCompiled = false;
}

void RenderGraph::Compile()
{
    for (RenderGraphPassNode& passNode : m_passNodes)
    {
        passNode.uses.clear();
        RenderGraphBuilder builder(passNode.uses);
        passNode.pass->Setup(builder);
    }

    m_compiledGraph = RenderGraphCompiler::Compile(m_passNodes, m_resources);
    m_isCompiled = true;
}

uint64_t RenderGraph::Execute(const ExecuteDesc& desc)
{
    if (!m_isCompiled)
    {
        Compile();
    }

    std::vector<uint64_t> batchSignalValues(m_compiledGraph.queueBatches.size(), 0);
    for (uint32_t batchIndex = 0; batchIndex < m_compiledGraph.queueBatches.size(); ++batchIndex)
    {
        batchSignalValues[batchIndex] = desc.timelineFence.AdvanceValue();
    }
    if (!batchSignalValues.empty())
    {
        desc.device.SetCurrentFrameFenceValue(batchSignalValues.back());
    }

    desc.transientResourcePool.Prepare(desc.device, m_compiledGraph);

    std::vector<rhi::IRHITexture*> textures(m_compiledGraph.resources.size(), nullptr);
    std::vector<rhi::IRHIBuffer*> buffers(m_compiledGraph.resources.size(), nullptr);

    for (uint32_t resourceIndex = 0; resourceIndex < m_compiledGraph.resources.size(); ++resourceIndex)
    {
        const RenderGraphResourceInfo& resource = m_compiledGraph.resources[resourceIndex];
        if (resource.kind == ResourceKind::Texture)
        {
            textures[resourceIndex] = resource.imported ? resource.importedTexture
                                                        : desc.transientResourcePool.GetTexture(resourceIndex);
        }
        else
        {
            buffers[resourceIndex] = resource.imported ? resource.importedBuffer
                                                       : desc.transientResourcePool.GetBuffer(resourceIndex);
        }
    }

    RenderGraphContext context(desc.device, m_compiledGraph, textures, buffers);
    std::vector<rhi::RHIBarrierDesc> resolvedBarriers;
    std::array<bool, 3> timestampPoolReset{};
    const bool timestampsEnabled = HasTimestampProfiling(desc.timestampProfiling);

    if (timestampsEnabled)
    {
        for (uint32_t& queryCount : desc.timestampProfiling.queryCounts)
        {
            queryCount = 0;
        }
        for (RenderGraphTimestampPassRange& passRange : desc.timestampProfiling.passRanges)
        {
            passRange = {};
        }
    }

    for (uint32_t batchIndex = 0; batchIndex < m_compiledGraph.queueBatches.size(); ++batchIndex)
    {
        const QueueBatchInfo& batch = m_compiledGraph.queueBatches[batchIndex];
        std::unique_ptr<rhi::IRHICommandList> ownedCommandList;
        CommandListPool::Lease pooledLease{};
        rhi::IRHICommandList* commandList = nullptr;

        if (desc.commandListPool)
        {
            pooledLease = desc.commandListPool->Acquire(desc.device, batch.queueType,
                                                        desc.timelineFence.GetCompletedValue());
            commandList = pooledLease.commandList;
        }
        else
        {
            ownedCommandList = desc.device.CreateCommandList(batch.queueType);
            WEST_ASSERT(ownedCommandList != nullptr);
            commandList = ownedCommandList.get();
        }

        WEST_ASSERT(commandList != nullptr);

        commandList->Reset();
        commandList->Begin();

        rhi::IRHITimestampQueryPool* timestampQueryPool = nullptr;
        uint32_t timestampQueueIndex = rhi::QueueTypeIndex(batch.queueType);
        if (timestampsEnabled && timestampQueueIndex < desc.timestampProfiling.queryPools.size() &&
            timestampQueueIndex < desc.timestampProfiling.queryCounts.size() &&
            timestampQueueIndex < timestampPoolReset.size())
        {
            timestampQueryPool = desc.timestampProfiling.queryPools[timestampQueueIndex];
            if (timestampQueryPool != nullptr && !timestampPoolReset[timestampQueueIndex])
            {
                commandList->ResetTimestampQueries(timestampQueryPool, 0, timestampQueryPool->GetDesc().queryCount);
                timestampPoolReset[timestampQueueIndex] = true;
            }
        }

        const uint32_t batchFirstTimestampQuery =
            timestampQueryPool != nullptr ? desc.timestampProfiling.queryCounts[timestampQueueIndex] : 0;

        for (uint32_t passIndex = batch.beginPass; passIndex <= batch.endPass; ++passIndex)
        {
            const CompiledPassInfo& pass = m_compiledGraph.passes[passIndex];
            RecordResolvedBarriers(*commandList, pass.preBarriers, textures, buffers, resolvedBarriers);

            if (timestampQueryPool != nullptr)
            {
                uint32_t& queryCount = desc.timestampProfiling.queryCounts[timestampQueueIndex];
                WEST_ASSERT(queryCount + 2 <= timestampQueryPool->GetDesc().queryCount);

                const uint32_t beginQueryIndex = queryCount++;
                const uint32_t endQueryIndex = queryCount++;
                commandList->WriteTimestamp(timestampQueryPool, beginQueryIndex);

                pass.pass->Execute(context, *commandList);

                commandList->WriteTimestamp(timestampQueryPool, endQueryIndex);

                if (passIndex < desc.timestampProfiling.passRanges.size())
                {
                    desc.timestampProfiling.passRanges[passIndex] = RenderGraphTimestampPassRange{
                        .valid = true,
                        .queueType = batch.queueType,
                        .beginQueryIndex = beginQueryIndex,
                        .endQueryIndex = endQueryIndex,
                    };
                }
                continue;
            }

            pass.pass->Execute(context, *commandList);
        }

        RecordResolvedBarriers(*commandList, batch.postBarriers, textures, buffers, resolvedBarriers);

        if (timestampQueryPool != nullptr)
        {
            const uint32_t batchQueryCount =
                desc.timestampProfiling.queryCounts[timestampQueueIndex] - batchFirstTimestampQuery;
            commandList->ResolveTimestampQueries(timestampQueryPool, batchFirstTimestampQuery, batchQueryCount);
        }

        commandList->End();

        const uint64_t signalValue = batchSignalValues[batchIndex];

        std::vector<rhi::IRHICommandList*> commandLists = {commandList};
        std::vector<rhi::RHITimelineWaitDesc> timelineWaits;
        timelineWaits.reserve(batch.waitBatchIndices.size());
        for (uint32_t producerBatchIndex : batch.waitBatchIndices)
        {
            timelineWaits.push_back({&desc.timelineFence, batchSignalValues[producerBatchIndex], batch.waitStageMask});
        }

        std::vector<rhi::RHITimelineSignalDesc> timelineSignals = {{&desc.timelineFence, signalValue}};

        rhi::RHISubmitInfo submitInfo{};
        submitInfo.commandLists = std::span<rhi::IRHICommandList* const>(commandLists.data(), commandLists.size());
        submitInfo.timelineWaits =
            std::span<const rhi::RHITimelineWaitDesc>(timelineWaits.data(), timelineWaits.size());
        submitInfo.timelineSignals =
            std::span<const rhi::RHITimelineSignalDesc>(timelineSignals.data(), timelineSignals.size());
        submitInfo.waitSemaphore = batchIndex == 0 ? desc.waitSemaphore : nullptr;
        submitInfo.signalSemaphore = batchIndex + 1 == m_compiledGraph.queueBatches.size()
                                         ? desc.signalSemaphore
                                         : nullptr;

        desc.device.GetQueue(batch.queueType)->Submit(submitInfo);

        const uint64_t commandListAvailableFenceValue =
            batchSignalValues.empty() ? signalValue : batchSignalValues.back();

        if (desc.commandListPool)
        {
            desc.commandListPool->Release(pooledLease, commandListAvailableFenceValue);
        }
        else
        {
            rhi::IRHICommandList* retainedCommandList = ownedCommandList.release();
            desc.device.EnqueueDeferredDeletion([retainedCommandList]()
            {
                delete retainedCommandList;
            }, commandListAvailableFenceValue);
        }
    }

    return batchSignalValues.empty() ? 0 : batchSignalValues.back();
}

void RenderGraph::Reset()
{
    m_resources.clear();
    m_passNodes.clear();
    m_compiledGraph = {};
    m_isCompiled = false;
}

} // namespace west::render
