// =============================================================================
// WestEngine - Editor
// Runtime telemetry overlay for portfolio evidence
// =============================================================================
#pragma once

#include "core/Types.h"
#include "rhi/interface/RHIDescriptors.h"

namespace west::editor
{

class FrameTelemetry;

struct TelemetryOverlayDesc
{
    const FrameTelemetry* frameTelemetry = nullptr;
    rhi::RHIDeviceCaps deviceCaps{};
    const char* backendName = "Unknown";
    uint32 visibleDrawCount = 0;
    uint32 candidateDrawCount = 0;
};

void BuildTelemetryOverlay(const TelemetryOverlayDesc& desc);

} // namespace west::editor
