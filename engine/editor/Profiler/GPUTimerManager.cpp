// =============================================================================
// WestEngine - Editor
// Async GPU timestamp readback manager
// =============================================================================
#include "editor/Profiler/GPUTimerManager.h"

#include "core/Logger.h"
#include "rhi/interface/IRHIDevice.h"

#include <algorithm>

namespace west::editor
{

namespace
{

constexpr std::array<const char*, 3> kQueryPoolNames = {
    "FrameTelemetryGraphicsTimestamps",
    "FrameTelemetryComputeTimestamps",
    "FrameTelemetryCopyTimestamps",
};

} // namespace

void GPUTimerManager::Initialize(rhi::IRHIDevice& device, uint32 maxFramesInFlight)
{
    Shutdown();

    const rhi::RHIDeviceCaps caps = device.GetCapabilities();
    for (uint32 queueIndex = 0; queueIndex < kQueueCount; ++queueIndex)
    {
        const auto queueType = static_cast<rhi::RHIQueueType>(queueIndex);
        m_timestampSupportedByQueue[queueIndex] = caps.SupportsTimestampQueries(queueType);
    }

    if (!caps.supportsTimestampQueries)
    {
        WEST_LOG_WARNING(LogCategory::RHI, "GPU timestamp queries are not supported by this backend/device.");
        return;
    }

    m_maxFramesInFlight = maxFramesInFlight;
    m_frameSlots.resize(maxFramesInFlight);
    m_initialized = true;
}

void GPUTimerManager::Shutdown()
{
    m_frameSlots.clear();
    m_timestampSupportedByQueue.fill(false);
    m_maxFramesInFlight = 0;
    m_initialized = false;
}

void GPUTimerManager::ConsumeCompletedFrame(uint32 frameIndex, FrameTelemetry& telemetry)
{
    if (!m_initialized || frameIndex >= m_frameSlots.size())
    {
        telemetry.ClearGpuFrameTime();
        return;
    }

    FrameSlot& slot = m_frameSlots[frameIndex];
    if (!slot.pending)
    {
        return;
    }

    for (uint32 queueIndex = 0; queueIndex < kQueueCount; ++queueIndex)
    {
        const uint32 queryCount = slot.queryCounts[queueIndex];
        slot.timestampReadbacks[queueIndex].assign(queryCount, 0);
        if (queryCount == 0)
        {
            continue;
        }

        rhi::IRHITimestampQueryPool* queryPool = slot.queryPools[queueIndex].get();
        if (queryPool == nullptr ||
            !queryPool->ReadTimestamps(0, queryCount, std::span<uint64>(slot.timestampReadbacks[queueIndex])))
        {
            telemetry.ClearGpuFrameTime();
            slot.pending = false;
            return;
        }
    }

    std::vector<GpuPassTelemetry> passTimings;
    passTimings.reserve(slot.pendingPasses.size());

    float frameGpuMs = 0.0f;
    for (const PendingPassTiming& pendingPass : slot.pendingPasses)
    {
        if (!pendingPass.valid)
        {
            continue;
        }

        const uint32 queueIndex = QueueIndex(pendingPass.queueType);
        const auto& timestamps = slot.timestampReadbacks[queueIndex];
        if (pendingPass.beginQueryIndex >= timestamps.size() || pendingPass.endQueryIndex >= timestamps.size())
        {
            continue;
        }

        const uint64 beginTicks = timestamps[pendingPass.beginQueryIndex];
        const uint64 endTicks = timestamps[pendingPass.endQueryIndex];
        if (endTicks < beginTicks)
        {
            continue;
        }

        const rhi::IRHITimestampQueryPool* queryPool = slot.queryPools[queueIndex].get();
        const float gpuMs =
            static_cast<float>(endTicks - beginTicks) * queryPool->GetTimestampPeriodNanoseconds() / 1'000'000.0f;
        frameGpuMs += gpuMs;

        passTimings.push_back(GpuPassTelemetry{
            .debugName = pendingPass.debugName,
            .queueType = pendingPass.queueType,
            .gpuMs = gpuMs,
            .valid = true,
        });
    }

    telemetry.RecordGpuFrameTiming(frameGpuMs, std::span<const GpuPassTelemetry>(passTimings.data(),
                                                                                 passTimings.size()));
    slot.pending = false;
}

render::RenderGraphTimestampProfilingDesc GPUTimerManager::BeginFrame(
    rhi::IRHIDevice& device, uint32 frameIndex, const render::CompiledRenderGraph& graph)
{
    if (!m_initialized || frameIndex >= m_frameSlots.size())
    {
        return {};
    }

    FrameSlot& slot = m_frameSlots[frameIndex];
    const std::array<uint32, kQueueCount> requiredQueryCapacity = ComputeRequiredQueryCapacity(graph);
    EnsureFrameSlot(device, slot, requiredQueryCapacity);

    slot.queryCounts.fill(0);
    slot.activeQueryPools.fill(nullptr);
    for (uint32 queueIndex = 0; queueIndex < kQueueCount; ++queueIndex)
    {
        slot.activeQueryPools[queueIndex] = slot.queryPools[queueIndex].get();
    }

    slot.passRanges.assign(graph.passes.size(), {});
    slot.pendingPasses.clear();
    slot.pending = false;

    return render::RenderGraphTimestampProfilingDesc{
        .queryPools = std::span<rhi::IRHITimestampQueryPool*>(slot.activeQueryPools.data(),
                                                              slot.activeQueryPools.size()),
        .queryCounts = std::span<uint32>(slot.queryCounts.data(), slot.queryCounts.size()),
        .passRanges = std::span<render::RenderGraphTimestampPassRange>(slot.passRanges.data(),
                                                                        slot.passRanges.size()),
    };
}

void GPUTimerManager::EndFrame(uint32 frameIndex, uint64 fenceValue, const render::CompiledRenderGraph& graph)
{
    if (!m_initialized || frameIndex >= m_frameSlots.size())
    {
        return;
    }

    FrameSlot& slot = m_frameSlots[frameIndex];
    slot.fenceValue = fenceValue;
    slot.pendingPasses.clear();
    slot.pendingPasses.reserve(slot.passRanges.size());

    for (uint32 passIndex = 0; passIndex < slot.passRanges.size(); ++passIndex)
    {
        const render::RenderGraphTimestampPassRange& range = slot.passRanges[passIndex];
        if (!range.valid || passIndex >= graph.passes.size())
        {
            continue;
        }

        const render::CompiledPassInfo& pass = graph.passes[passIndex];
        slot.pendingPasses.push_back(PendingPassTiming{
            .debugName = pass.pass != nullptr ? pass.pass->GetDebugName() : "<culled>",
            .queueType = range.queueType,
            .beginQueryIndex = range.beginQueryIndex,
            .endQueryIndex = range.endQueryIndex,
            .valid = true,
        });
    }

    slot.pending = !slot.pendingPasses.empty();
}

uint32 GPUTimerManager::QueueIndex(rhi::RHIQueueType queueType)
{
    return static_cast<uint32>(queueType);
}

std::array<uint32, GPUTimerManager::kQueueCount> GPUTimerManager::ComputeRequiredQueryCapacity(
    const render::CompiledRenderGraph& graph)
{
    std::array<uint32, kQueueCount> passCounts{};
    for (const render::CompiledPassInfo& pass : graph.passes)
    {
        if (pass.pass == nullptr)
        {
            continue;
        }
        const uint32 queueIndex = QueueIndex(pass.pass->GetQueueType());
        WEST_ASSERT(queueIndex < passCounts.size());
        ++passCounts[queueIndex];
    }

    std::array<uint32, kQueueCount> required{};
    for (uint32 queueIndex = 0; queueIndex < kQueueCount; ++queueIndex)
    {
        required[queueIndex] = passCounts[queueIndex] > 0 ? passCounts[queueIndex] * 2 : 0;
    }
    return required;
}

void GPUTimerManager::EnsureFrameSlot(rhi::IRHIDevice& device, FrameSlot& slot,
                                      const std::array<uint32, kQueueCount>& requiredQueryCapacity)
{
    for (uint32 queueIndex = 0; queueIndex < kQueueCount; ++queueIndex)
    {
        if (!m_timestampSupportedByQueue[queueIndex] || requiredQueryCapacity[queueIndex] == 0)
        {
            slot.queryPools[queueIndex].reset();
            slot.queryCapacities[queueIndex] = 0;
            slot.timestampReadbacks[queueIndex].clear();
            continue;
        }

        if (slot.queryCapacities[queueIndex] >= requiredQueryCapacity[queueIndex] &&
            slot.queryPools[queueIndex] != nullptr)
        {
            continue;
        }

        rhi::RHITimestampQueryPoolDesc desc{};
        desc.queryCount = requiredQueryCapacity[queueIndex];
        desc.queueType = static_cast<rhi::RHIQueueType>(queueIndex);
        desc.debugName = kQueryPoolNames[queueIndex];
        slot.queryPools[queueIndex] = device.CreateTimestampQueryPool(desc);
        slot.queryCapacities[queueIndex] = slot.queryPools[queueIndex] != nullptr ? requiredQueryCapacity[queueIndex] : 0;
        slot.timestampReadbacks[queueIndex].resize(slot.queryCapacities[queueIndex]);
    }
}

} // namespace west::editor
