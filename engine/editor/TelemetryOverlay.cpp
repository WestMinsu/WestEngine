// =============================================================================
// WestEngine - Editor
// Runtime telemetry overlay for portfolio evidence
// =============================================================================
#include "editor/TelemetryOverlay.h"

#include "core/Assert.h"
#include "editor/Profiler/FrameTelemetry.h"

#include <format>
#include <imgui.h>
#include <span>
#include <string>

namespace west::editor
{

namespace
{

const char* OnOff(bool value)
{
    return value ? "on" : "off";
}

const char* QueueTypeName(rhi::RHIQueueType queueType)
{
    switch (queueType)
    {
    case rhi::RHIQueueType::Graphics:
        return "Graphics";
    case rhi::RHIQueueType::Compute:
        return "Compute";
    case rhi::RHIQueueType::Copy:
        return "Copy";
    default:
        return "Unknown";
    }
}

const char* ResourceKindName(render::ResourceKind kind)
{
    return kind == render::ResourceKind::Texture ? "Texture" : "Buffer";
}

std::string FormatBytes(uint64 bytes)
{
    constexpr double kKiB = 1024.0;
    constexpr double kMiB = kKiB * 1024.0;
    constexpr double kGiB = kMiB * 1024.0;
    const double value = static_cast<double>(bytes);

    if (value >= kGiB)
    {
        return std::format("{:.2f} GB", value / kGiB);
    }
    if (value >= kMiB)
    {
        return std::format("{:.2f} MB", value / kMiB);
    }
    if (value >= kKiB)
    {
        return std::format("{:.2f} KB", value / kKiB);
    }
    return std::format("{} B", bytes);
}

} // namespace

void BuildTelemetryOverlay(const TelemetryOverlayDesc& desc)
{
    WEST_ASSERT(desc.frameTelemetry != nullptr);

    const FrameTelemetryStats& stats = desc.frameTelemetry->GetDisplayStats();
    const RenderGraphEvidence& evidence = desc.frameTelemetry->GetRenderGraphEvidence();

#if defined(TRACY_ENABLE)
    constexpr bool kTracyEnabled = true;
#else
    constexpr bool kTracyEnabled = false;
#endif

    ImGui::SetNextWindowPos(ImVec2(492.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460.0f, 330.0f), ImGuiCond_FirstUseEver);

    if (!ImGui::Begin("WestEngine Telemetry", nullptr, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::End();
        return;
    }

    const ImVec4 warningColor = ImVec4(0.95f, 0.72f, 0.28f, 1.0f);

    ImGui::Text("Backend: %s | GPU timestamps: %s", desc.backendName, OnOff(desc.deviceCaps.supportsTimestampQueries));
    ImGui::Text("Timing (%.1fs avg): %.1f FPS | CPU %.2f ms", FrameTelemetry::kDisplayRefreshSeconds, stats.fps,
                stats.latestCpuFrameMs);

    if (stats.hasGpuFrameTime)
    {
        ImGui::Text("GPU %.2f ms", stats.latestGpuFrameMs);
    }
    else
    {
        ImGui::TextColored(warningColor, "GPU frame time: pending async timestamp readback");
    }

    const float cullPercent = desc.candidateDrawCount > 0
                                  ? 100.0f - (static_cast<float>(desc.visibleDrawCount) /
                                              static_cast<float>(desc.candidateDrawCount) * 100.0f)
                                  : 0.0f;
    ImGui::Text("GPU-driven draws: %u visible / %u candidates (%.1f%% culled)", desc.visibleDrawCount,
                desc.candidateDrawCount, cullPercent);
    ImGui::Text("Tracy: %s", kTracyEnabled ? "compiled in" : "off");

    const std::span<const GpuPassTelemetry> gpuPassTimings = desc.frameTelemetry->GetGpuPassTimings();
    if (!gpuPassTimings.empty() && ImGui::TreeNode("GPU pass timings"))
    {
        for (uint32 index = 0; index < gpuPassTimings.size(); ++index)
        {
            const GpuPassTelemetry& pass = gpuPassTimings[index];
            ImGui::BulletText("%02u %-28s [%s] %.3f ms", index, pass.debugName.c_str(), QueueTypeName(pass.queueType),
                              pass.gpuMs);
        }
        ImGui::TreePop();
    }

    ImGui::Separator();
    if (!evidence.valid)
    {
        ImGui::TextColored(warningColor, "Render Graph evidence: pending first compile");
        ImGui::End();
        return;
    }

    ImGui::Text("RenderGraph: %u passes | %u resources | %u batches", evidence.passCount, evidence.resourceCount,
                evidence.queueBatchCount);
    ImGui::Text("Barriers: %u transitions | %u UAV | %u final", evidence.transitionBarrierCount,
                evidence.uavBarrierCount, evidence.finalBarrierCount);

    if (ImGui::TreeNode("RenderGraph internals"))
    {
        ImGui::Text("Resources: %u imported | %u transient | %u aliased", evidence.importedResourceCount,
                    evidence.transientResourceCount, evidence.aliasedResourceCount);
        ImGui::Text("Aliasing barriers: %u", evidence.aliasingBarrierCount);
        ImGui::Text("Transient peak: %s -> %s", FormatBytes(evidence.peakBytesWithoutAliasing).c_str(),
                    FormatBytes(evidence.peakBytesWithAliasing).c_str());
        ImGui::Text("Aliasing saved: %s", FormatBytes(evidence.bytesSavedWithAliasing).c_str());
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("RenderGraph passes"))
    {
        for (uint32 index = 0; index < evidence.passes.size(); ++index)
        {
            const RenderGraphPassTelemetry& pass = evidence.passes[index];
            ImGui::BulletText("%02u %-28s [%s] uses R:%u W:%u RW:%u preBarriers:%u", index, pass.debugName.c_str(),
                              QueueTypeName(pass.queueType), pass.readUseCount, pass.writeUseCount,
                              pass.readWriteUseCount, pass.preBarrierCount);
        }
        ImGui::TreePop();
    }

    if (ImGui::TreeNode("Render Graph resources"))
    {
        for (uint32 index = 0; index < evidence.resources.size(); ++index)
        {
            const RenderGraphResourceTelemetry& resource = evidence.resources[index];
            const char* aliasText = resource.aliasSlot != render::kInvalidRenderGraphIndex ? "alias" : "unique";
            const std::string aliasSlotLabel =
                resource.aliasSlot != render::kInvalidRenderGraphIndex ? std::format("{}", resource.aliasSlot) : "-";
            const std::string resourceSizeLabel = FormatBytes(resource.estimatedSizeBytes);
            if (resource.lifetimeValid)
            {
                ImGui::BulletText("%02u %-30s %-7s %s pass %u..%u slot %s size %s", index, resource.debugName.c_str(),
                                  ResourceKindName(resource.kind), resource.imported ? "imported" : aliasText,
                                  resource.firstUsePass, resource.lastUsePass, aliasSlotLabel.c_str(),
                                  resourceSizeLabel.c_str());
            }
            else
            {
                ImGui::BulletText("%02u %-30s %-7s imported:%s unused size %s", index, resource.debugName.c_str(),
                                  ResourceKindName(resource.kind), OnOff(resource.imported), resourceSizeLabel.c_str());
            }
        }
        ImGui::TreePop();
    }

    ImGui::End();
}

} // namespace west::editor
