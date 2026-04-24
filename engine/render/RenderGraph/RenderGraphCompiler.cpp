// =============================================================================
// WestEngine - Render
// Render Graph compile logic
// =============================================================================
#include "render/RenderGraph/RenderGraphCompiler.h"

#include "core/Assert.h"
#include "render/RenderGraph/RenderGraphPass.h"
#include "rhi/interface/RHIFormatUtils.h"

#include <algorithm>
#include <array>
#include <numeric>
#include <tuple>

namespace west::render
{

namespace
{

struct ResourceStateTracker
{
    rhi::RHIResourceState state = rhi::RHIResourceState::Undefined;
    ResourceAccessType lastAccessType = ResourceAccessType::Read;
};

[[nodiscard]] bool IsReadAccess(ResourceAccessType accessType)
{
    return accessType == ResourceAccessType::Read || accessType == ResourceAccessType::ReadWrite;
}

[[nodiscard]] bool IsWriteAccess(ResourceAccessType accessType)
{
    return accessType == ResourceAccessType::Write || accessType == ResourceAccessType::ReadWrite;
}

[[nodiscard]] bool NeedsUAVBarrier(rhi::RHIResourceState previousState, ResourceAccessType previousAccess,
                                   rhi::RHIResourceState nextState, ResourceAccessType nextAccess)
{
    if (previousState != rhi::RHIResourceState::UnorderedAccess || nextState != rhi::RHIResourceState::UnorderedAccess)
    {
        return false;
    }

    return IsWriteAccess(previousAccess) || IsWriteAccess(nextAccess);
}

[[nodiscard]] uint64_t EstimateTextureSizeBytes(const rhi::RHITextureDesc& desc)
{
    const uint32_t bytesPerElement = rhi::GetFormatByteSize(desc.format);
    if (bytesPerElement == 0)
    {
        return 0;
    }

    uint64_t totalBytes = 0;
    uint32_t width = desc.width;
    uint32_t height = desc.height;
    uint32_t depth = desc.depth;

    for (uint32_t mipLevel = 0; mipLevel < desc.mipLevels; ++mipLevel)
    {
        totalBytes += static_cast<uint64_t>(width) * height * depth * bytesPerElement * desc.arrayLayers;
        width = (std::max)(1u, width >> 1);
        height = (std::max)(1u, height >> 1);
        depth = (std::max)(1u, depth >> 1);
    }

    return totalBytes;
}

[[nodiscard]] uint64_t EstimateBufferSizeBytes(const rhi::RHIBufferDesc& desc)
{
    return desc.sizeBytes;
}

[[nodiscard]] bool LifetimesOverlap(const ResourceLifetime& lhs, const ResourceLifetime& rhs)
{
    if (!lhs.IsValid() || !rhs.IsValid())
    {
        return false;
    }

    return lhs.firstUsePass <= rhs.lastUsePass && rhs.firstUsePass <= lhs.lastUsePass;
}

[[nodiscard]] bool AreAliasCompatible(const RenderGraphResourceInfo& lhs, const RenderGraphResourceInfo& rhs)
{
    if (lhs.kind != rhs.kind || lhs.imported || rhs.imported)
    {
        return false;
    }

    if (lhs.kind != ResourceKind::Texture)
    {
        return false;
    }

    const auto& lhsDesc = lhs.textureDesc;
    const auto& rhsDesc = rhs.textureDesc;

    return lhsDesc.dimension == rhsDesc.dimension &&
           lhsDesc.format == rhsDesc.format &&
           lhsDesc.width == rhsDesc.width &&
           lhsDesc.height == rhsDesc.height &&
           lhsDesc.depth == rhsDesc.depth &&
           lhsDesc.mipLevels == rhsDesc.mipLevels &&
           lhsDesc.usage == rhsDesc.usage &&
           lhsDesc.arrayLayers == rhsDesc.arrayLayers;
}

[[nodiscard]] std::vector<uint32_t> BuildStableTopologicalOrder(std::span<const RenderGraphPassNode> passNodes,
                                                                std::vector<std::vector<uint32_t>>& outgoing)
{
    outgoing.assign(passNodes.size(), {});
    std::vector<uint32_t> indegree(passNodes.size(), 0);
    std::vector<int32_t> lastWriter;
    std::vector<std::vector<uint32_t>> activeReaders;

    uint32_t maxResourceIndex = 0;
    for (const RenderGraphPassNode& passNode : passNodes)
    {
        for (const ResourceUse& use : passNode.uses)
        {
            maxResourceIndex = (std::max)(maxResourceIndex, use.resourceIndex);
        }
    }

    lastWriter.assign(maxResourceIndex + 1, -1);
    activeReaders.resize(maxResourceIndex + 1);

    auto addEdge = [&](uint32_t source, uint32_t destination)
    {
        if (source == destination)
        {
            return;
        }

        auto& edges = outgoing[source];
        if (std::find(edges.begin(), edges.end(), destination) != edges.end())
        {
            return;
        }

        edges.push_back(destination);
        ++indegree[destination];
    };

    for (uint32_t passIndex = 0; passIndex < passNodes.size(); ++passIndex)
    {
        for (const ResourceUse& use : passNodes[passIndex].uses)
        {
            if (use.resourceIndex >= lastWriter.size())
            {
                continue;
            }

            if (IsReadAccess(use.accessType))
            {
                if (lastWriter[use.resourceIndex] >= 0)
                {
                    addEdge(static_cast<uint32_t>(lastWriter[use.resourceIndex]), passIndex);
                }

                auto& readers = activeReaders[use.resourceIndex];
                if (std::find(readers.begin(), readers.end(), passIndex) == readers.end())
                {
                    readers.push_back(passIndex);
                }
            }

            if (IsWriteAccess(use.accessType))
            {
                if (lastWriter[use.resourceIndex] >= 0)
                {
                    addEdge(static_cast<uint32_t>(lastWriter[use.resourceIndex]), passIndex);
                }

                for (uint32_t readerPassIndex : activeReaders[use.resourceIndex])
                {
                    addEdge(readerPassIndex, passIndex);
                }
                activeReaders[use.resourceIndex].clear();
                lastWriter[use.resourceIndex] = static_cast<int32_t>(passIndex);
            }
        }
    }

    std::vector<uint32_t> ready;
    ready.reserve(passNodes.size());
    for (uint32_t passIndex = 0; passIndex < indegree.size(); ++passIndex)
    {
        if (indegree[passIndex] == 0)
        {
            ready.push_back(passIndex);
        }
    }

    std::vector<uint32_t> sortedOrder;
    sortedOrder.reserve(passNodes.size());

    while (!ready.empty())
    {
        const auto readyIt = std::min_element(ready.begin(), ready.end());
        const uint32_t passIndex = *readyIt;
        ready.erase(readyIt);
        sortedOrder.push_back(passIndex);

        for (uint32_t destination : outgoing[passIndex])
        {
            WEST_ASSERT(indegree[destination] > 0);
            --indegree[destination];
            if (indegree[destination] == 0)
            {
                ready.push_back(destination);
            }
        }
    }

    WEST_ASSERT(sortedOrder.size() == passNodes.size());
    return sortedOrder;
}

[[nodiscard]] bool WritesImportedResource(const RenderGraphPassNode& passNode,
                                          std::span<const RenderGraphResourceInfo> resources)
{
    for (const ResourceUse& use : passNode.uses)
    {
        if (use.resourceIndex >= resources.size())
        {
            continue;
        }

        if (resources[use.resourceIndex].imported && IsWriteAccess(use.accessType))
        {
            return true;
        }
    }

    return false;
}

[[nodiscard]] std::vector<uint32_t> CullDeadPasses(std::span<const RenderGraphPassNode> passNodes,
                                                   std::span<const RenderGraphResourceInfo> resources,
                                                   std::span<const std::vector<uint32_t>> outgoing,
                                                   std::span<const uint32_t> sortedPassIndices)
{
    std::vector<std::vector<uint32_t>> incoming(passNodes.size());
    for (uint32_t sourcePassIndex = 0; sourcePassIndex < outgoing.size(); ++sourcePassIndex)
    {
        for (uint32_t destinationPassIndex : outgoing[sourcePassIndex])
        {
            incoming[destinationPassIndex].push_back(sourcePassIndex);
        }
    }

    std::vector<bool> livePassMask(passNodes.size(), false);
    std::vector<uint32_t> stack;

    for (uint32_t passIndex = 0; passIndex < passNodes.size(); ++passIndex)
    {
        const bool isRootPass = passNodes[passIndex].pass->HasSideEffects() ||
                                WritesImportedResource(passNodes[passIndex], resources);
        if (!isRootPass)
        {
            continue;
        }

        livePassMask[passIndex] = true;
        stack.push_back(passIndex);
    }

    while (!stack.empty())
    {
        const uint32_t passIndex = stack.back();
        stack.pop_back();

        for (uint32_t producerPassIndex : incoming[passIndex])
        {
            if (livePassMask[producerPassIndex])
            {
                continue;
            }

            livePassMask[producerPassIndex] = true;
            stack.push_back(producerPassIndex);
        }
    }

    std::vector<uint32_t> liveSortedPassIndices;
    liveSortedPassIndices.reserve(sortedPassIndices.size());
    for (uint32_t passIndex : sortedPassIndices)
    {
        if (livePassMask[passIndex])
        {
            liveSortedPassIndices.push_back(passIndex);
        }
    }

    return liveSortedPassIndices;
}

void AssignResourceLifetimes(const std::vector<CompiledPassInfo>& passes, std::vector<RenderGraphResourceInfo>& resources)
{
    for (RenderGraphResourceInfo& resource : resources)
    {
        resource.lifetime = {};
        resource.alias = {};
        resource.estimatedSizeBytes =
            resource.kind == ResourceKind::Texture ? EstimateTextureSizeBytes(resource.textureDesc)
                                                   : EstimateBufferSizeBytes(resource.bufferDesc);
    }

    for (uint32_t passIndex = 0; passIndex < passes.size(); ++passIndex)
    {
        for (const ResourceUse& use : passes[passIndex].uses)
        {
            auto& lifetime = resources[use.resourceIndex].lifetime;
            if (!lifetime.IsValid())
            {
                lifetime.firstUsePass = passIndex;
            }
            lifetime.lastUsePass = passIndex;
        }
    }
}

void AssignAliasSlots(std::vector<RenderGraphResourceInfo>& resources)
{
    struct AliasSlot
    {
        uint32_t slotIndex = 0;
        uint32_t lastResourceIndex = kInvalidRenderGraphIndex;
        ResourceLifetime lifetime{};
        uint64_t sizeBytes = 0;
    };

    std::vector<uint32_t> textureResourceIndices;
    textureResourceIndices.reserve(resources.size());

    for (uint32_t resourceIndex = 0; resourceIndex < resources.size(); ++resourceIndex)
    {
        const RenderGraphResourceInfo& resource = resources[resourceIndex];
        if (resource.kind == ResourceKind::Texture && !resource.imported && resource.lifetime.IsValid())
        {
            textureResourceIndices.push_back(resourceIndex);
        }
    }

    std::stable_sort(textureResourceIndices.begin(), textureResourceIndices.end(),
                     [&](uint32_t lhsIndex, uint32_t rhsIndex)
                     {
                         return resources[lhsIndex].lifetime.firstUsePass < resources[rhsIndex].lifetime.firstUsePass;
                     });

    std::vector<AliasSlot> slots;
    for (uint32_t resourceIndex : textureResourceIndices)
    {
        RenderGraphResourceInfo& resource = resources[resourceIndex];

        for (AliasSlot& slot : slots)
        {
            const RenderGraphResourceInfo& previous = resources[slot.lastResourceIndex];
            if (LifetimesOverlap(slot.lifetime, resource.lifetime) || !AreAliasCompatible(previous, resource))
            {
                continue;
            }

            resource.alias.slot = slot.slotIndex;
            resource.alias.previousResourceIndex = slot.lastResourceIndex;
            slot.lastResourceIndex = resourceIndex;
            slot.lifetime = resource.lifetime;
            slot.sizeBytes = (std::max)(slot.sizeBytes, resource.estimatedSizeBytes);
            break;
        }

        if (resource.alias.slot != kInvalidRenderGraphIndex)
        {
            continue;
        }

        AliasSlot slot{};
        slot.slotIndex = static_cast<uint32_t>(slots.size());
        slot.lastResourceIndex = resourceIndex;
        slot.lifetime = resource.lifetime;
        slot.sizeBytes = resource.estimatedSizeBytes;
        resource.alias.slot = slot.slotIndex;
        slots.push_back(slot);
    }
}

uint64_t ComputePeakWithoutAliasing(const std::vector<RenderGraphResourceInfo>& resources, uint32_t passCount)
{
    uint64_t peakBytes = 0;
    for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
    {
        uint64_t activeBytes = 0;
        for (const RenderGraphResourceInfo& resource : resources)
        {
            if (resource.kind != ResourceKind::Texture || resource.imported || !resource.lifetime.IsValid())
            {
                continue;
            }

            if (resource.lifetime.firstUsePass <= passIndex && passIndex <= resource.lifetime.lastUsePass)
            {
                activeBytes += resource.estimatedSizeBytes;
            }
        }
        peakBytes = (std::max)(peakBytes, activeBytes);
    }
    return peakBytes;
}

uint64_t ComputePeakWithAliasing(const std::vector<RenderGraphResourceInfo>& resources, uint32_t passCount)
{
    uint32_t maxSlotIndex = 0;
    bool hasSlots = false;

    for (const RenderGraphResourceInfo& resource : resources)
    {
        if (resource.kind == ResourceKind::Texture && !resource.imported && resource.alias.slot != kInvalidRenderGraphIndex)
        {
            hasSlots = true;
            maxSlotIndex = (std::max)(maxSlotIndex, resource.alias.slot);
        }
    }

    if (!hasSlots)
    {
        return 0;
    }

    std::vector<uint64_t> slotSizes(maxSlotIndex + 1, 0);
    for (const RenderGraphResourceInfo& resource : resources)
    {
        if (resource.kind != ResourceKind::Texture || resource.imported || resource.alias.slot == kInvalidRenderGraphIndex)
        {
            continue;
        }

        slotSizes[resource.alias.slot] = (std::max)(slotSizes[resource.alias.slot], resource.estimatedSizeBytes);
    }

    uint64_t peakBytes = 0;
    for (uint32_t passIndex = 0; passIndex < passCount; ++passIndex)
    {
        std::vector<bool> activeSlots(slotSizes.size(), false);
        for (const RenderGraphResourceInfo& resource : resources)
        {
            if (resource.kind != ResourceKind::Texture || resource.imported || !resource.lifetime.IsValid() ||
                resource.alias.slot == kInvalidRenderGraphIndex)
            {
                continue;
            }

            if (resource.lifetime.firstUsePass <= passIndex && passIndex <= resource.lifetime.lastUsePass)
            {
                activeSlots[resource.alias.slot] = true;
            }
        }

        uint64_t activeBytes = 0;
        for (uint32_t slotIndex = 0; slotIndex < activeSlots.size(); ++slotIndex)
        {
            if (activeSlots[slotIndex])
            {
                activeBytes += slotSizes[slotIndex];
            }
        }
        peakBytes = (std::max)(peakBytes, activeBytes);
    }

    return peakBytes;
}

[[nodiscard]] std::vector<uint32_t> BuildPassToBatchMap(std::span<const QueueBatchInfo> queueBatches, uint32_t passCount)
{
    std::vector<uint32_t> passToBatch(passCount, 0);
    for (uint32_t batchIndex = 0; batchIndex < queueBatches.size(); ++batchIndex)
    {
        const QueueBatchInfo& batch = queueBatches[batchIndex];
        for (uint32_t passIndex = batch.beginPass; passIndex <= batch.endPass; ++passIndex)
        {
            passToBatch[passIndex] = batchIndex;
        }
    }

    return passToBatch;
}

void EnsureTerminalBatchWaitsForAllPriorBatches(std::vector<QueueBatchInfo>& queueBatches)
{
    if (queueBatches.size() < 2)
    {
        return;
    }

    QueueBatchInfo& terminalBatch = queueBatches.back();
    for (uint32_t batchIndex = 0; batchIndex + 1 < queueBatches.size(); ++batchIndex)
    {
        if (std::find(terminalBatch.waitBatchIndices.begin(), terminalBatch.waitBatchIndices.end(), batchIndex) ==
            terminalBatch.waitBatchIndices.end())
        {
            terminalBatch.waitBatchIndices.push_back(batchIndex);
        }
    }
}

} // namespace

void RenderGraphCompiler::RefreshBarriers(CompiledRenderGraph& compiledGraph)
{
    for (CompiledPassInfo& pass : compiledGraph.passes)
    {
        pass.preBarriers.clear();
    }
    for (QueueBatchInfo& batch : compiledGraph.queueBatches)
    {
        batch.postBarriers.clear();
    }
    compiledGraph.finalBarriers.clear();

    std::vector<ResourceStateTracker> stateTrackers(compiledGraph.resources.size());
    for (uint32_t resourceIndex = 0; resourceIndex < compiledGraph.resources.size(); ++resourceIndex)
    {
        stateTrackers[resourceIndex].state = compiledGraph.resources[resourceIndex].imported
                                                 ? compiledGraph.resources[resourceIndex].initialState
                                                 : rhi::RHIResourceState::Undefined;
    }

    for (uint32_t resourceIndex = 0; resourceIndex < compiledGraph.resources.size(); ++resourceIndex)
    {
        const RenderGraphResourceInfo& resource = compiledGraph.resources[resourceIndex];
        if (resource.alias.previousResourceIndex == kInvalidRenderGraphIndex || !resource.lifetime.IsValid())
        {
            continue;
        }

        CompiledBarrier aliasBarrier{};
        aliasBarrier.type = rhi::RHIBarrierDesc::Type::Aliasing;
        aliasBarrier.resourceKind = resource.kind;
        aliasBarrier.aliasBeforeResourceIndex = resource.alias.previousResourceIndex;
        aliasBarrier.aliasAfterResourceIndex = resourceIndex;
        compiledGraph.passes[resource.lifetime.firstUsePass].preBarriers.push_back(aliasBarrier);
    }

    for (uint32_t passIndex = 0; passIndex < compiledGraph.passes.size(); ++passIndex)
    {
        CompiledPassInfo& pass = compiledGraph.passes[passIndex];
        for (const ResourceUse& use : pass.uses)
        {
            ResourceStateTracker& tracker = stateTrackers[use.resourceIndex];

            if (NeedsUAVBarrier(tracker.state, tracker.lastAccessType, use.state, use.accessType))
            {
                CompiledBarrier uavBarrier{};
                uavBarrier.type = rhi::RHIBarrierDesc::Type::UAV;
                uavBarrier.resourceKind = use.resourceKind;
                uavBarrier.resourceIndex = use.resourceIndex;
                pass.preBarriers.push_back(uavBarrier);
            }

            if (tracker.state != use.state)
            {
                CompiledBarrier transitionBarrier{};
                transitionBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
                transitionBarrier.resourceKind = use.resourceKind;
                transitionBarrier.resourceIndex = use.resourceIndex;
                transitionBarrier.stateBefore = tracker.state;
                transitionBarrier.stateAfter = use.state;
                pass.preBarriers.push_back(transitionBarrier);
            }

            tracker.state = use.state;
            tracker.lastAccessType = use.accessType;
        }
    }

    if (compiledGraph.queueBatches.empty())
    {
        return;
    }

    const std::vector<uint32_t> passToBatch =
        BuildPassToBatchMap(compiledGraph.queueBatches, static_cast<uint32_t>(compiledGraph.passes.size()));

    for (uint32_t resourceIndex = 0; resourceIndex < compiledGraph.resources.size(); ++resourceIndex)
    {
        const RenderGraphResourceInfo& resource = compiledGraph.resources[resourceIndex];
        if (!resource.imported || !resource.lifetime.IsValid())
        {
            continue;
        }

        if (stateTrackers[resourceIndex].state == resource.finalState)
        {
            continue;
        }

        CompiledBarrier barrier{};
        barrier.type = rhi::RHIBarrierDesc::Type::Transition;
        barrier.resourceKind = resource.kind;
        barrier.resourceIndex = resourceIndex;
        barrier.stateBefore = stateTrackers[resourceIndex].state;
        barrier.stateAfter = resource.finalState;
        const uint32_t batchIndex = passToBatch[resource.lifetime.lastUsePass];
        compiledGraph.queueBatches[batchIndex].postBarriers.push_back(barrier);
        compiledGraph.finalBarriers.push_back(barrier);
    }
}

CompiledRenderGraph RenderGraphCompiler::Compile(std::span<const RenderGraphPassNode> passNodes,
                                                 std::vector<RenderGraphResourceInfo> resources)
{
    CompiledRenderGraph compiledGraph{};

    std::vector<std::vector<uint32_t>> outgoing;
    compiledGraph.sortedPassIndices = BuildStableTopologicalOrder(passNodes, outgoing);
    compiledGraph.sortedPassIndices =
        CullDeadPasses(passNodes, resources, outgoing, compiledGraph.sortedPassIndices);
    compiledGraph.passes.reserve(compiledGraph.sortedPassIndices.size());

    for (uint32_t originalPassIndex : compiledGraph.sortedPassIndices)
    {
        CompiledPassInfo compiledPass{};
        compiledPass.pass = passNodes[originalPassIndex].pass;
        compiledPass.originalPassIndex = originalPassIndex;
        compiledPass.uses = passNodes[originalPassIndex].uses;
        compiledGraph.passes.push_back(std::move(compiledPass));
    }

    AssignResourceLifetimes(compiledGraph.passes, resources);
    AssignAliasSlots(resources);

    if (!compiledGraph.passes.empty())
    {
        QueueBatchInfo currentBatch{};
        currentBatch.queueType = compiledGraph.passes.front().pass->GetQueueType();
        currentBatch.beginPass = 0;

        for (uint32_t passIndex = 1; passIndex < compiledGraph.passes.size(); ++passIndex)
        {
            if (compiledGraph.passes[passIndex].pass->GetQueueType() == currentBatch.queueType)
            {
                continue;
            }

            currentBatch.endPass = passIndex - 1;
            compiledGraph.queueBatches.push_back(std::move(currentBatch));

            currentBatch = {};
            currentBatch.queueType = compiledGraph.passes[passIndex].pass->GetQueueType();
            currentBatch.beginPass = passIndex;
        }

        currentBatch.endPass = static_cast<uint32_t>(compiledGraph.passes.size() - 1);
        compiledGraph.queueBatches.push_back(std::move(currentBatch));
    }

    const std::vector<uint32_t> passToBatch =
        BuildPassToBatchMap(compiledGraph.queueBatches, static_cast<uint32_t>(compiledGraph.passes.size()));

    std::vector<uint32_t> originalPassToCompiledIndex(passNodes.size(), kInvalidRenderGraphIndex);
    for (uint32_t passIndex = 0; passIndex < compiledGraph.sortedPassIndices.size(); ++passIndex)
    {
        originalPassToCompiledIndex[compiledGraph.sortedPassIndices[passIndex]] = passIndex;
    }

    for (uint32_t originalSourceIndex = 0; originalSourceIndex < outgoing.size(); ++originalSourceIndex)
    {
        const uint32_t compiledSourceIndex = originalPassToCompiledIndex[originalSourceIndex];
        if (compiledSourceIndex == kInvalidRenderGraphIndex)
        {
            continue;
        }

        for (uint32_t originalDestinationIndex : outgoing[originalSourceIndex])
        {
            const uint32_t compiledDestinationIndex = originalPassToCompiledIndex[originalDestinationIndex];
            if (compiledDestinationIndex == kInvalidRenderGraphIndex)
            {
                continue;
            }

            const uint32_t sourceBatchIndex = passToBatch[compiledSourceIndex];
            const uint32_t destinationBatchIndex = passToBatch[compiledDestinationIndex];
            if (sourceBatchIndex == destinationBatchIndex)
            {
                continue;
            }

            auto& waitBatches = compiledGraph.queueBatches[destinationBatchIndex].waitBatchIndices;
            if (std::find(waitBatches.begin(), waitBatches.end(), sourceBatchIndex) == waitBatches.end())
            {
                waitBatches.push_back(sourceBatchIndex);
            }
        }
    }

    EnsureTerminalBatchWaitsForAllPriorBatches(compiledGraph.queueBatches);

    compiledGraph.peakBytesWithoutAliasing =
        ComputePeakWithoutAliasing(resources, static_cast<uint32_t>(compiledGraph.passes.size()));
    compiledGraph.peakBytesWithAliasing =
        ComputePeakWithAliasing(resources, static_cast<uint32_t>(compiledGraph.passes.size()));
    compiledGraph.bytesSavedWithAliasing =
        compiledGraph.peakBytesWithoutAliasing >= compiledGraph.peakBytesWithAliasing
            ? compiledGraph.peakBytesWithoutAliasing - compiledGraph.peakBytesWithAliasing
            : 0;

    compiledGraph.resources = std::move(resources);
    RefreshBarriers(compiledGraph);
    return compiledGraph;
}

} // namespace west::render
