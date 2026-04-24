// =============================================================================
// WestEngine - Editor
// Runtime frame telemetry and Render Graph evidence snapshot
// =============================================================================
#pragma once

#include "core/Types.h"
#include "render/RenderGraph/RenderGraphCompiler.h"

#include <array>
#include <span>
#include <string>
#include <vector>

namespace west::editor
{

struct FrameTelemetryStats
{
    float latestCpuFrameMs = 0.0f;
    float averageCpuFrameMs = 0.0f;
    float minCpuFrameMs = 0.0f;
    float maxCpuFrameMs = 0.0f;
    float fps = 0.0f;
    float latestGpuFrameMs = 0.0f;
    bool hasGpuFrameTime = false;
    uint32 sampleCount = 0;
};

struct RenderGraphPassTelemetry
{
    std::string debugName;
    rhi::RHIQueueType queueType = rhi::RHIQueueType::Graphics;
    uint32 readUseCount = 0;
    uint32 writeUseCount = 0;
    uint32 readWriteUseCount = 0;
    uint32 preBarrierCount = 0;
};

struct RenderGraphResourceTelemetry
{
    std::string debugName;
    render::ResourceKind kind = render::ResourceKind::Texture;
    bool imported = false;
    bool lifetimeValid = false;
    uint32 firstUsePass = render::kInvalidRenderGraphIndex;
    uint32 lastUsePass = render::kInvalidRenderGraphIndex;
    uint32 aliasSlot = render::kInvalidRenderGraphIndex;
    uint32 previousAliasResource = render::kInvalidRenderGraphIndex;
    uint64 estimatedSizeBytes = 0;
};

struct RenderGraphEvidence
{
    bool valid = false;
    uint64 peakBytesWithoutAliasing = 0;
    uint64 peakBytesWithAliasing = 0;
    uint64 bytesSavedWithAliasing = 0;
    uint32 passCount = 0;
    uint32 queueBatchCount = 0;
    uint32 resourceCount = 0;
    uint32 importedResourceCount = 0;
    uint32 transientResourceCount = 0;
    uint32 aliasedResourceCount = 0;
    uint32 transitionBarrierCount = 0;
    uint32 aliasingBarrierCount = 0;
    uint32 uavBarrierCount = 0;
    uint32 finalBarrierCount = 0;
    std::vector<RenderGraphPassTelemetry> passes;
    std::vector<RenderGraphResourceTelemetry> resources;
};

class FrameTelemetry final
{
public:
    static constexpr uint32 kFrameHistorySize = 120;

    void RecordFrameDelta(float deltaSeconds);
    void RecordGpuFrameTime(float gpuFrameMs);
    void ClearGpuFrameTime();
    void CaptureRenderGraph(const render::CompiledRenderGraph& graph);

    [[nodiscard]] const FrameTelemetryStats& GetStats() const
    {
        return m_stats;
    }

    [[nodiscard]] const RenderGraphEvidence& GetRenderGraphEvidence() const
    {
        return m_renderGraphEvidence;
    }

    [[nodiscard]] std::span<const float> GetCpuFrameHistory() const
    {
        return std::span<const float>(m_cpuFrameMs.data(), m_cpuFrameMs.size());
    }

    [[nodiscard]] uint32 GetCpuFrameHistoryOffset() const
    {
        return m_historyCursor;
    }

private:
    void RecomputeCpuStats();

    std::array<float, kFrameHistorySize> m_cpuFrameMs{};
    uint32 m_historyCursor = 0;
    uint32 m_recordedSamples = 0;
    FrameTelemetryStats m_stats{};
    RenderGraphEvidence m_renderGraphEvidence{};
};

} // namespace west::editor
