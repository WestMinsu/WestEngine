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

    m_displayWindowSeconds += deltaSeconds;
    m_displayCpuFrameMsSum += m_stats.latestCpuFrameMs;
    ++m_displayFrameCount;

    if (!m_hasDisplayStats || m_displayWindowSeconds >= kDisplayRefreshSeconds)
    {
        RefreshDisplayStats();
    }
}

void FrameTelemetry::RecordGpuFrameTime(float gpuFrameMs)
{
    m_stats.latestGpuFrameMs = gpuFrameMs;
    m_stats.hasGpuFrameTime = gpuFrameMs >= 0.0f;

    if (m_stats.hasGpuFrameTime)
    {
        m_displayGpuFrameMsSum += gpuFrameMs;
        ++m_displayGpuSampleCount;
    }
}

void FrameTelemetry::RecordGpuFrameTiming(float gpuFrameMs, std::span<const GpuPassTelemetry> passTimings)
{
    RecordGpuFrameTime(gpuFrameMs);
    m_gpuPassTimings.assign(passTimings.begin(), passTimings.end());
}

void FrameTelemetry::ClearGpuFrameTime()
{
    m_stats.latestGpuFrameMs = 0.0f;
    m_stats.hasGpuFrameTime = false;
    m_displayStats.latestGpuFrameMs = 0.0f;
    m_displayStats.hasGpuFrameTime = false;
    m_displayGpuFrameMsSum = 0.0f;
    m_displayGpuSampleCount = 0;
    m_gpuPassTimings.clear();
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

void FrameTelemetry::RefreshDisplayStats()
{
    if (m_displayFrameCount == 0 || m_displayWindowSeconds <= 0.0f)
    {
        return;
    }

    const float windowCpuMs = m_displayCpuFrameMsSum / static_cast<float>(m_displayFrameCount);
    const float windowFps = static_cast<float>(m_displayFrameCount) / m_displayWindowSeconds;
    const bool hadDisplayGpuFrameTime = m_displayStats.hasGpuFrameTime;
    const float previousDisplayGpuFrameMs = m_displayStats.latestGpuFrameMs;

    m_displayStats = m_stats;
    m_displayStats.latestCpuFrameMs = windowCpuMs;
    m_displayStats.fps = windowFps;
    m_displayStats.sampleCount = m_displayFrameCount;

    if (m_displayGpuSampleCount > 0)
    {
        m_displayStats.latestGpuFrameMs =
            m_displayGpuFrameMsSum / static_cast<float>(m_displayGpuSampleCount);
        m_displayStats.hasGpuFrameTime = true;
    }
    else if (hadDisplayGpuFrameTime)
    {
        m_displayStats.latestGpuFrameMs = previousDisplayGpuFrameMs;
        m_displayStats.hasGpuFrameTime = true;
    }

    m_displayWindowSeconds = 0.0f;
    m_displayCpuFrameMsSum = 0.0f;
    m_displayGpuFrameMsSum = 0.0f;
    m_displayFrameCount = 0;
    m_displayGpuSampleCount = 0;
    m_hasDisplayStats = true;
}

} // namespace west::editor
