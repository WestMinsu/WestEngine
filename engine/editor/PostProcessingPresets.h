// =============================================================================
// WestEngine - Editor
// Runtime post-processing preset definitions and display labels
// =============================================================================
#pragma once

#include "core/Types.h"
#include "render/Passes/BokehDOFPass.h"
#include "render/Passes/ToneMappingPass.h"

#include <span>
#include <string>

namespace west::editor
{

struct PostPresetDefinition
{
    const char* name = "Portfolio Default";
    render::ToneMappingPass::PostSettings toneMapping{};
    render::BokehDOFPass::Settings bokeh{};
    bool enableBokeh = false;
};

[[nodiscard]] std::span<const PostPresetDefinition> GetPostPresets();
[[nodiscard]] const PostPresetDefinition& GetPostPreset(uint32 presetIndex);
[[nodiscard]] uint32 GetPostPresetCount();
[[nodiscard]] std::string BuildRuntimePostPresetLabel(uint32 presetIndex, bool dirty);

[[nodiscard]] const char* ToneMappingOperatorName(render::ToneMappingPass::ToneMappingOperator value);
[[nodiscard]] const char* PostDebugViewName(render::ToneMappingPass::DebugView value);
[[nodiscard]] const char* PostDebugChannelName(render::ToneMappingPass::DebugChannel value);

[[nodiscard]] std::span<const char* const> GetToneMappingOperatorLabels();
[[nodiscard]] std::span<const char* const> GetPostDebugViewLabels();
[[nodiscard]] std::span<const char* const> GetPostDebugChannelLabels();

} // namespace west::editor
