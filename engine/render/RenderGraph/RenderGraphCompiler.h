// =============================================================================
// WestEngine - Render
// Render Graph compile output and compiler entrypoint
// =============================================================================
#pragma once

#include "render/RenderGraph/RenderGraphResource.h"
#include "rhi/interface/RHIDescriptors.h"

#include <span>
#include <vector>

namespace west::render
{

struct CompiledBarrier
{
    rhi::RHIBarrierDesc::Type type = rhi::RHIBarrierDesc::Type::Transition;
    ResourceKind resourceKind = ResourceKind::Texture;
    uint32_t resourceIndex = kInvalidRenderGraphIndex;
    uint32_t aliasBeforeResourceIndex = kInvalidRenderGraphIndex;
    uint32_t aliasAfterResourceIndex = kInvalidRenderGraphIndex;
    rhi::RHIResourceState stateBefore = rhi::RHIResourceState::Undefined;
    rhi::RHIResourceState stateAfter = rhi::RHIResourceState::Common;
};

struct CompiledPassInfo
{
    RenderGraphPass* pass = nullptr;
    uint32_t originalPassIndex = kInvalidRenderGraphIndex;
    std::vector<ResourceUse> uses;
    std::vector<CompiledBarrier> preBarriers;
};

struct QueueBatchInfo
{
    rhi::RHIQueueType queueType = rhi::RHIQueueType::Graphics;
    uint32_t beginPass = 0;
    uint32_t endPass = 0;
    std::vector<uint32_t> waitBatchIndices;
};

struct CompiledRenderGraph
{
    std::vector<CompiledPassInfo> passes;
    std::vector<QueueBatchInfo> queueBatches;
    std::vector<CompiledBarrier> finalBarriers;
    std::vector<RenderGraphResourceInfo> resources;
    std::vector<uint32_t> sortedPassIndices;
    uint64_t peakBytesWithoutAliasing = 0;
    uint64_t peakBytesWithAliasing = 0;
    uint64_t bytesSavedWithAliasing = 0;
};

class RenderGraphCompiler
{
public:
    [[nodiscard]] static CompiledRenderGraph Compile(std::span<const RenderGraphPassNode> passNodes,
                                                     std::vector<RenderGraphResourceInfo> resources);
};

} // namespace west::render
