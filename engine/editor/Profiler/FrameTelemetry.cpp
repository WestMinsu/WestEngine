// =============================================================================
// WestEngine - Editor
// Runtime frame telemetry and Render Graph evidence snapshot
// =============================================================================
#include "editor/Profiler/FrameTelemetry.h"

#include "render/RenderGraph/RenderGraphPass.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace west::editor
{

namespace
{

void CountBarrier(const render::CompiledBarrier& barrier, RenderGraphEvidence& evidence)
{
    switch (barrier.type)
    {
    case rhi::RHIBarrierDesc::Type::Transition:
        ++evidence.transitionBarrierCount;
        break;
    case rhi::RHIBarrierDesc::Type::Aliasing:
        ++evidence.aliasingBarrierCount;
        break;
    case rhi::RHIBarrierDesc::Type::UAV:
        ++evidence.uavBarrierCount;
        break;
    default:
        break;
    }
}

void CountUse(const render::ResourceUse& use, RenderGraphPassTelemetry& pass)
{
    switch (use.accessType)
    {
    case render::ResourceAccessType::Read:
        ++pass.readUseCount;
        break;
    case render::ResourceAccessType::Write:
        ++pass.writeUseCount;
        break;
    case render::ResourceAccessType::ReadWrite:
        ++pass.readWriteUseCount;
        break;
    default:
        break;
    }
}

} // namespace

void FrameTelemetry::RecordFrameDelta(float deltaSeconds)
{
    if (deltaSeconds <= 0.0f)
    {
        return;
    }

    m_cpuFrameMs[m_historyCursor] = deltaSeconds * 1000.0f;
    m_historyCursor = (m_historyCursor + 1) % kFrameHistorySize;
    m_recordedSamples = std::min<uint32>(m_recordedSamples + 1, kFrameHistorySize);
    RecomputeCpuStats();
}

void FrameTelemetry::RecordGpuFrameTime(float gpuFrameMs)
{
    m_stats.latestGpuFrameMs = gpuFrameMs;
    m_stats.hasGpuFrameTime = gpuFrameMs >= 0.0f;
}

void FrameTelemetry::ClearGpuFrameTime()
{
    m_stats.latestGpuFrameMs = 0.0f;
    m_stats.hasGpuFrameTime = false;
}

void FrameTelemetry::CaptureRenderGraph(const render::CompiledRenderGraph& graph)
{
    RenderGraphEvidence evidence{};
    evidence.valid = true;
    evidence.peakBytesWithoutAliasing = graph.peakBytesWithoutAliasing;
    evidence.peakBytesWithAliasing = graph.peakBytesWithAliasing;
    evidence.bytesSavedWithAliasing = graph.bytesSavedWithAliasing;
    evidence.passCount = static_cast<uint32>(graph.passes.size());
    evidence.queueBatchCount = static_cast<uint32>(graph.queueBatches.size());
    evidence.resourceCount = static_cast<uint32>(graph.resources.size());
    evidence.finalBarrierCount = static_cast<uint32>(graph.finalBarriers.size());

    evidence.passes.reserve(graph.passes.size());
    for (const render::CompiledPassInfo& compiledPass : graph.passes)
    {
        RenderGraphPassTelemetry pass{};
        pass.debugName = compiledPass.pass != nullptr ? compiledPass.pass->GetDebugName() : "<culled>";
        pass.queueType =
            compiledPass.pass != nullptr ? compiledPass.pass->GetQueueType() : rhi::RHIQueueType::Graphics;
        pass.preBarrierCount = static_cast<uint32>(compiledPass.preBarriers.size());

        for (const render::ResourceUse& use : compiledPass.uses)
        {
            CountUse(use, pass);
        }
        for (const render::CompiledBarrier& barrier : compiledPass.preBarriers)
        {
            CountBarrier(barrier, evidence);
        }

        evidence.passes.push_back(std::move(pass));
    }

    for (const render::QueueBatchInfo& batch : graph.queueBatches)
    {
        for (const render::CompiledBarrier& barrier : batch.postBarriers)
        {
            CountBarrier(barrier, evidence);
        }
    }

    for (const render::RenderGraphResourceInfo& compiledResource : graph.resources)
    {
        RenderGraphResourceTelemetry resource{};
        resource.debugName = compiledResource.debugName;
        resource.kind = compiledResource.kind;
        resource.imported = compiledResource.imported;
        resource.lifetimeValid = compiledResource.lifetime.IsValid();
        resource.firstUsePass = compiledResource.lifetime.firstUsePass;
        resource.lastUsePass = compiledResource.lifetime.lastUsePass;
        resource.aliasSlot = compiledResource.alias.slot;
        resource.previousAliasResource = compiledResource.alias.previousResourceIndex;
        resource.estimatedSizeBytes = compiledResource.estimatedSizeBytes;

        if (resource.imported)
        {
            ++evidence.importedResourceCount;
        }
        else
        {
            ++evidence.transientResourceCount;
        }
        if (resource.aliasSlot != render::kInvalidRenderGraphIndex)
        {
            ++evidence.aliasedResourceCount;
        }

        evidence.resources.push_back(std::move(resource));
    }

    m_renderGraphEvidence = std::move(evidence);
}

void FrameTelemetry::RecomputeCpuStats()
{
    if (m_recordedSamples == 0)
    {
        m_stats = {};
        return;
    }

    float sumMs = 0.0f;
    float minMs = std::numeric_limits<float>::max();
    float maxMs = 0.0f;

    for (uint32 index = 0; index < m_recordedSamples; ++index)
    {
        const float frameMs = m_cpuFrameMs[index];
        sumMs += frameMs;
        minMs = std::min(minMs, frameMs);
        maxMs = std::max(maxMs, frameMs);
    }

    m_stats.latestCpuFrameMs =
        m_cpuFrameMs[(m_historyCursor + kFrameHistorySize - 1) % kFrameHistorySize];
    m_stats.averageCpuFrameMs = sumMs / static_cast<float>(m_recordedSamples);
    m_stats.minCpuFrameMs = minMs;
    m_stats.maxCpuFrameMs = maxMs;
    m_stats.fps = m_stats.latestCpuFrameMs > 0.0f ? 1000.0f / m_stats.latestCpuFrameMs : 0.0f;
    m_stats.sampleCount = m_recordedSamples;
}

} // namespace west::editor
