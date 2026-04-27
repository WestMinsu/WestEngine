// =============================================================================
// WestEngine - Editor
// Runtime ImGui control sections for render settings
// =============================================================================
#pragma once

namespace west::editor
{

struct RuntimeRenderSettings;

[[nodiscard]] bool BuildRenderFeatureControls(RuntimeRenderSettings& settings);
[[nodiscard]] bool BuildLightingAndPBRControls(RuntimeRenderSettings& settings);
[[nodiscard]] bool BuildPostProcessingControls(RuntimeRenderSettings& settings);
void BuildRuntimeHotkeyHelp();

} // namespace west::editor
