// =============================================================================
// WestEngine - Editor
// Async GPU timestamp readback manager
// =============================================================================
#pragma once

#include "core/Types.h"
#include "editor/Profiler/FrameTelemetry.h"
#include "render/RenderGraph/RenderGraph.h"
#include "rhi/interface/IRHITimestampQueryPool.h"

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace west::rhi
{
class IRHIDevice;
}

namespace west::editor
{

class GPUTimerManager final
{
public:
    void Initialize(rhi::IRHIDevice& device, uint32 maxFramesInFlight);
    void Shutdown();

    void ConsumeCompletedFrame(uint32 frameIndex, FrameTelemetry& telemetry);

    [[nodiscard]] render::RenderGraphTimestampProfilingDesc BeginFrame(
        rhi::IRHIDevice& device, uint32 frameIndex, const render::CompiledRenderGraph& graph);
    void EndFrame(uint32 frameIndex, uint64 fenceValue, const render::CompiledRenderGraph& graph);

private:
    static constexpr uint32 kQueueCount = 3;

    struct PendingPassTiming
    {
        std::string debugName;
        rhi::RHIQueueType queueType = rhi::RHIQueueType::Graphics;
        uint32 beginQueryIndex = render::kInvalidRenderGraphIndex;
        uint32 endQueryIndex = render::kInvalidRenderGraphIndex;
        bool valid = false;
    };

    struct FrameSlot
    {
        std::array<std::unique_ptr<rhi::IRHITimestampQueryPool>, kQueueCount> queryPools;
        std::array<rhi::IRHITimestampQueryPool*, kQueueCount> activeQueryPools{};
        std::array<uint32, kQueueCount> queryCounts{};
        std::array<uint32, kQueueCount> queryCapacities{};
        std::array<std::vector<uint64>, kQueueCount> timestampReadbacks;
        std::vector<render::RenderGraphTimestampPassRange> passRanges;
        std::vector<PendingPassTiming> pendingPasses;
        uint64 fenceValue = 0;
        bool pending = false;
    };

    [[nodiscard]] static uint32 QueueIndex(rhi::RHIQueueType queueType);
    [[nodiscard]] static std::array<uint32, kQueueCount> ComputeRequiredQueryCapacity(
        const render::CompiledRenderGraph& graph);
    void EnsureFrameSlot(rhi::IRHIDevice& device, FrameSlot& slot,
                         const std::array<uint32, kQueueCount>& requiredQueryCapacity);

    std::vector<FrameSlot> m_frameSlots;
    std::array<bool, kQueueCount> m_timestampSupportedByQueue{};
    uint32 m_maxFramesInFlight = 0;
    bool m_initialized = false;
};

} // namespace west::editor
