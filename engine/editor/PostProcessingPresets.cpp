// =============================================================================
// WestEngine - Editor
// Runtime post-processing preset definitions and display labels
// =============================================================================
#include "editor/PostProcessingPresets.h"

#include <algorithm>
#include <array>
#include <format>

namespace west::editor
{

namespace
{

[[nodiscard]] std::array<PostPresetDefinition, 8> BuildPostPresets()
{
    using ToneMappingOperator = render::ToneMappingPass::ToneMappingOperator;

    std::array<PostPresetDefinition, 8> presets{};

    presets[0].name = "Portfolio Default";

    presets[1].name = "Neutral ACES";
    presets[1].toneMapping.contrast = 1.0f;
    presets[1].toneMapping.saturation = 1.0f;
    presets[1].toneMapping.vibrance = 0.0f;
    presets[1].toneMapping.vignetteStrength = 0.0f;
    presets[1].toneMapping.filmGrainStrength = 0.0f;
    presets[1].toneMapping.chromaticAberration = 0.0f;
    presets[1].bokeh.intensity = 0.0f;
    presets[1].enableBokeh = false;

    presets[2].name = "Cinematic U2";
    presets[2].toneMapping.toneMappingOperator = ToneMappingOperator::Uncharted2;
    presets[2].toneMapping.exposure = 1.05f;
    presets[2].toneMapping.contrast = 1.08f;
    presets[2].toneMapping.saturation = 0.95f;
    presets[2].toneMapping.vibrance = 0.02f;
    presets[2].toneMapping.vignetteStrength = 0.20f;
    presets[2].toneMapping.vignetteRadius = 0.78f;
    presets[2].toneMapping.filmGrainStrength = 0.010f;
    presets[2].toneMapping.chromaticAberration = 0.10f;
    presets[2].bokeh.focusRangeScale = 0.12f;
    presets[2].bokeh.maxBlurRadius = 7.0f;
    presets[2].bokeh.intensity = 0.0f;
    presets[2].bokeh.highlightBoost = 1.35f;
    presets[2].bokeh.foregroundBias = 0.45f;
    presets[2].enableBokeh = false;

    presets[3].name = "Vibrant GT";
    presets[3].toneMapping.toneMappingOperator = ToneMappingOperator::GranTurismo;
    presets[3].toneMapping.exposure = 1.02f;
    presets[3].toneMapping.contrast = 1.06f;
    presets[3].toneMapping.saturation = 1.12f;
    presets[3].toneMapping.vibrance = 0.18f;
    presets[3].toneMapping.vignetteStrength = 0.08f;
    presets[3].toneMapping.vignetteRadius = 0.84f;
    presets[3].toneMapping.filmGrainStrength = 0.0f;
    presets[3].toneMapping.chromaticAberration = 0.04f;
    presets[3].bokeh.focusRangeScale = 0.16f;
    presets[3].bokeh.maxBlurRadius = 5.0f;
    presets[3].bokeh.intensity = 0.0f;
    presets[3].bokeh.highlightBoost = 1.10f;
    presets[3].bokeh.foregroundBias = 0.30f;
    presets[3].enableBokeh = false;

    presets[4].name = "Clean";
    presets[4].toneMapping.exposure = 1.0f;
    presets[4].toneMapping.contrast = 1.0f;
    presets[4].toneMapping.saturation = 1.0f;
    presets[4].toneMapping.vibrance = 0.0f;
    presets[4].toneMapping.vignetteStrength = 0.0f;
    presets[4].toneMapping.filmGrainStrength = 0.0f;
    presets[4].toneMapping.chromaticAberration = 0.0f;
    presets[4].bokeh.intensity = 0.0f;
    presets[4].enableBokeh = false;

    presets[5].name = "Hable Film";
    presets[5].toneMapping.toneMappingOperator = ToneMappingOperator::Hable;
    presets[5].toneMapping.exposure = 1.08f;
    presets[5].toneMapping.contrast = 1.04f;
    presets[5].toneMapping.saturation = 0.98f;
    presets[5].toneMapping.vibrance = 0.03f;
    presets[5].toneMapping.vignetteStrength = 0.14f;
    presets[5].toneMapping.vignetteRadius = 0.80f;
    presets[5].toneMapping.filmGrainStrength = 0.012f;
    presets[5].toneMapping.chromaticAberration = 0.06f;
    presets[5].bokeh.focusRangeScale = 0.15f;
    presets[5].bokeh.maxBlurRadius = 6.0f;
    presets[5].bokeh.intensity = 0.0f;
    presets[5].bokeh.highlightBoost = 1.20f;
    presets[5].bokeh.foregroundBias = 0.38f;
    presets[5].enableBokeh = false;

    presets[6].name = "Reinhard Ext";
    presets[6].toneMapping.toneMappingOperator = ToneMappingOperator::ReinhardExtended;
    presets[6].toneMapping.exposure = 1.00f;
    presets[6].toneMapping.maxWhite = 5.5f;
    presets[6].toneMapping.contrast = 1.02f;
    presets[6].toneMapping.saturation = 1.00f;
    presets[6].toneMapping.vibrance = 0.04f;
    presets[6].toneMapping.vignetteStrength = 0.06f;
    presets[6].toneMapping.vignetteRadius = 0.86f;
    presets[6].toneMapping.filmGrainStrength = 0.0f;
    presets[6].toneMapping.chromaticAberration = 0.0f;
    presets[6].bokeh.intensity = 0.0f;
    presets[6].enableBokeh = false;

    presets[7].name = "Lottes Crisp";
    presets[7].toneMapping.toneMappingOperator = ToneMappingOperator::Lottes;
    presets[7].toneMapping.exposure = 0.96f;
    presets[7].toneMapping.contrast = 1.05f;
    presets[7].toneMapping.saturation = 1.02f;
    presets[7].toneMapping.vibrance = 0.06f;
    presets[7].toneMapping.vignetteStrength = 0.04f;
    presets[7].toneMapping.vignetteRadius = 0.88f;
    presets[7].toneMapping.filmGrainStrength = 0.0f;
    presets[7].toneMapping.chromaticAberration = 0.02f;
    presets[7].bokeh.intensity = 0.0f;
    presets[7].enableBokeh = false;

    return presets;
}

const std::array<PostPresetDefinition, 8> kPostPresets = BuildPostPresets();

constexpr std::array<const char*, 10> kToneMappingLabels = {
    "None",   "Reinhard",    "ACES",         "Uncharted2", "GranTurismo",
    "Lottes", "Exponential", "Reinhard Ext", "Luminance",  "Hable",
};

constexpr std::array<const char*, 4> kDebugViewLabels = {
    "Off",
    "Tone Split",
    "Channels",
    "Post Split",
};

constexpr std::array<const char*, 6> kDebugChannelLabels = {
    "All", "Red", "Green", "Blue", "Alpha", "Luminance",
};

} // namespace

std::span<const PostPresetDefinition> GetPostPresets()
{
    return kPostPresets;
}

const PostPresetDefinition& GetPostPreset(uint32 presetIndex)
{
    return kPostPresets[std::min<size_t>(presetIndex, kPostPresets.size() - 1)];
}

uint32 GetPostPresetCount()
{
    return static_cast<uint32>(kPostPresets.size());
}

std::string BuildRuntimePostPresetLabel(uint32 presetIndex, bool dirty)
{
    const char* name = GetPostPreset(presetIndex).name;
    return dirty ? std::format("{}*", name) : std::string(name);
}

const char* ToneMappingOperatorName(render::ToneMappingPass::ToneMappingOperator value)
{
    using ToneMappingOperator = render::ToneMappingPass::ToneMappingOperator;
    switch (value)
    {
    case ToneMappingOperator::None:
        return "None";
    case ToneMappingOperator::Reinhard:
        return "Reinhard";
    case ToneMappingOperator::ACES:
        return "ACES";
    case ToneMappingOperator::Uncharted2:
        return "Uncharted2";
    case ToneMappingOperator::GranTurismo:
        return "GranTurismo";
    case ToneMappingOperator::Lottes:
        return "Lottes";
    case ToneMappingOperator::Exponential:
        return "Exponential";
    case ToneMappingOperator::ReinhardExtended:
        return "ReinhardExt";
    case ToneMappingOperator::Luminance:
        return "Luminance";
    case ToneMappingOperator::Hable:
        return "Hable";
    default:
        return "Unknown";
    }
}

const char* PostDebugViewName(render::ToneMappingPass::DebugView value)
{
    using DebugView = render::ToneMappingPass::DebugView;
    switch (value)
    {
    case DebugView::Off:
        return "Off";
    case DebugView::ToneMappingSplit:
        return "ToneSplit";
    case DebugView::ColorChannels:
        return "Channels";
    case DebugView::PostSplit:
        return "PostSplit";
    default:
        return "Unknown";
    }
}

const char* PostDebugChannelName(render::ToneMappingPass::DebugChannel value)
{
    using DebugChannel = render::ToneMappingPass::DebugChannel;
    switch (value)
    {
    case DebugChannel::All:
        return "All";
    case DebugChannel::Red:
        return "Red";
    case DebugChannel::Green:
        return "Green";
    case DebugChannel::Blue:
        return "Blue";
    case DebugChannel::Alpha:
        return "Alpha";
    case DebugChannel::Luminance:
        return "Luminance";
    default:
        return "Unknown";
    }
}

std::span<const char* const> GetToneMappingOperatorLabels()
{
    return kToneMappingLabels;
}

std::span<const char* const> GetPostDebugViewLabels()
{
    return kDebugViewLabels;
}

std::span<const char* const> GetPostDebugChannelLabels()
{
    return kDebugChannelLabels;
}

} // namespace west::editor
