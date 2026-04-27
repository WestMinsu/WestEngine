// =============================================================================
// WestEngine - Editor
// Runtime ImGui control sections for render settings
// =============================================================================
#include "editor/RuntimeControlPanel.h"

#include "editor/PostProcessingPresets.h"
#include "editor/RuntimeRenderSettings.h"

#include <imgui.h>
#include <span>

namespace west::editor
{

bool BuildRenderFeatureControls(RuntimeRenderSettings& settings)
{
    bool changed = false;
    ImGui::TextUnformatted("Render Features");
    changed |= ImGui::Checkbox("Textures", &settings.texturesEnabled);
    changed |= ImGui::Checkbox("Shadows", &settings.shadowsEnabled);
    changed |= ImGui::Checkbox("SSAO", &settings.ssaoEnabled);
    changed |= ImGui::Checkbox("IBL", &settings.iblEnabled);
    changed |= ImGui::Checkbox("Alpha Discard", &settings.alphaDiscardEnabled);
    return changed;
}

bool BuildLightingAndPBRControls(RuntimeRenderSettings& settings)
{
    bool changed = false;
    if (ImGui::CollapsingHeader("Lighting & PBR", ImGuiTreeNodeFlags_DefaultOpen))
    {
        changed |= ImGui::SliderFloat("Light Intensity", &settings.lightIntensity, 0.0f, 12.0f, "%.2f");
        changed |= ImGui::SliderFloat("Light Elevation", &settings.lightElevationDegrees, 5.0f, 89.0f, "%.1f deg");
        changed |= ImGui::SliderFloat("Light Azimuth", &settings.lightAzimuthDegrees, -180.0f, 180.0f, "%.1f deg");
        changed |= ImGui::SliderFloat("Environment Intensity", &settings.environmentIntensity, 0.0f, 4.0f, "%.2f");

        ImGui::Separator();
        ImGui::TextUnformatted("Global PBR");
        changed |= ImGui::SliderFloat("Diffuse Weight", &settings.diffuseWeight, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Specular Weight", &settings.specularWeight, 0.0f, 2.0f, "%.2f");
        changed |= ImGui::SliderFloat("Metallic Weight", &settings.metallicScale, 0.0f, 1.0f, "%.2f");
        changed |= ImGui::SliderFloat("Roughness Weight", &settings.roughnessScale, 0.0f, 1.0f, "%.2f");
        ImGui::Separator();
        ImGui::TextUnformatted("SSAO");
        changed |= ImGui::SliderFloat("SSAO Radius", &settings.ssaoRadius, 0.01f, 0.50f, "%.3f");
        changed |= ImGui::SliderFloat("SSAO Bias", &settings.ssaoBias, 0.0f, 0.10f, "%.4f");
        changed |= ImGui::SliderInt("SSAO Samples", &settings.ssaoSampleCount, 0, 64);
        changed |= ImGui::SliderFloat("SSAO Power", &settings.ssaoPower, 0.5f, 4.0f, "%.2f");
        ImGui::Separator();
        changed |= ImGui::SliderFloat("Shadow Bias", &settings.shadowBias, 0.0f, 0.01f, "%.4f");
        changed |= ImGui::SliderFloat("Shadow Normal Bias", &settings.shadowNormalBias, 0.0f, 0.05f, "%.4f");
    }

    return changed;
}

bool BuildPostProcessingControls(RuntimeRenderSettings& settings)
{
    bool changed = false;
    const std::span<const char* const> toneMappingLabels = GetToneMappingOperatorLabels();
    const std::span<const char* const> debugViewLabels = GetPostDebugViewLabels();
    const std::span<const char* const> debugChannelLabels = GetPostDebugChannelLabels();

    ImGui::TextUnformatted("Tone Mapping");

    int toneMappingOperator = static_cast<int>(settings.post.toneMappingOperator);
    if (ImGui::Combo("Operator", &toneMappingOperator, toneMappingLabels.data(),
                     static_cast<int>(toneMappingLabels.size())))
    {
        settings.post.toneMappingOperator =
            static_cast<render::ToneMappingPass::ToneMappingOperator>(toneMappingOperator);
        changed = true;
    }

    int debugView = static_cast<int>(settings.post.debugView);
    if (ImGui::Combo("Debug View", &debugView, debugViewLabels.data(), static_cast<int>(debugViewLabels.size())))
    {
        settings.post.debugView = static_cast<render::ToneMappingPass::DebugView>(debugView);
        changed = true;
    }

    int debugChannel = static_cast<int>(settings.post.debugChannel);
    if (ImGui::Combo("Debug Channel", &debugChannel, debugChannelLabels.data(),
                     static_cast<int>(debugChannelLabels.size())))
    {
        settings.post.debugChannel = static_cast<render::ToneMappingPass::DebugChannel>(debugChannel);
        changed = true;
    }

    changed |= ImGui::SliderFloat("Exposure", &settings.post.exposure, 0.25f, 4.0f, "%.2f");
    changed |= ImGui::SliderFloat("Gamma", &settings.post.gamma, 1.0f, 2.4f, "%.2f");
    changed |= ImGui::SliderFloat("Contrast", &settings.post.contrast, 0.50f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat("Brightness", &settings.post.brightness, -0.50f, 0.50f, "%.2f");
    changed |= ImGui::SliderFloat("Saturation", &settings.post.saturation, 0.0f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat("Vibrance", &settings.post.vibrance, -1.0f, 1.0f, "%.2f");

    if (settings.post.toneMappingOperator == render::ToneMappingPass::ToneMappingOperator::ReinhardExtended)
    {
        changed |= ImGui::SliderFloat("Max White", &settings.post.maxWhite, 1.0f, 12.0f, "%.2f");
    }
    if (settings.post.debugView != render::ToneMappingPass::DebugView::Off)
    {
        changed |= ImGui::SliderFloat("Debug Split", &settings.post.debugSplit, 0.05f, 0.95f, "%.2f");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Effects");

    if (ImGui::Checkbox("Bokeh DOF", &settings.bokehEnabled))
    {
        changed = true;
    }
    changed |= ImGui::Checkbox("FXAA", &settings.post.FXAAEnabled);
    changed |= ImGui::SliderFloat("Chromatic Aberration", &settings.post.chromaticAberration, 0.0f, 0.40f, "%.2f");
    changed |= ImGui::SliderFloat("Film Grain", &settings.post.filmGrainStrength, 0.0f, 0.08f, "%.3f");
    changed |= ImGui::SliderFloat("Vignette Strength", &settings.post.vignetteStrength, 0.0f, 0.50f, "%.2f");
    changed |= ImGui::SliderFloat("Vignette Radius", &settings.post.vignetteRadius, 0.50f, 1.00f, "%.2f");

    ImGui::Separator();
    ImGui::TextUnformatted("Bokeh");

    if (ImGui::SliderFloat("Bokeh Intensity", &settings.bokeh.intensity, 0.0f, 1.0f, "%.2f"))
    {
        if (settings.bokeh.intensity > 0.0f)
        {
            settings.bokehEnabled = true;
        }
        changed = true;
    }
    changed |= ImGui::SliderFloat("Focus Range", &settings.bokeh.focusRangeScale, 0.02f, 0.35f, "%.2f");
    changed |= ImGui::SliderFloat("Max Blur Radius", &settings.bokeh.maxBlurRadius, 1.0f, 10.0f, "%.2f");
    changed |= ImGui::SliderFloat("Highlight Boost", &settings.bokeh.highlightBoost, 0.5f, 2.0f, "%.2f");
    changed |= ImGui::SliderFloat("Foreground Bias", &settings.bokeh.foregroundBias, 0.0f, 1.0f, "%.2f");

    return changed;
}

void BuildRuntimeHotkeyHelp()
{
    ImGui::TextUnformatted("Hotkeys");
    ImGui::TextUnformatted("F1 GUI  |  F5 Help  |  F6 Preset  |  F8 Bokeh");
    ImGui::TextUnformatted("F9 ToneMap  |  F10 DebugView  |  F11 DebugChannel");
    ImGui::TextUnformatted("1..0 / O,P / N,M / J,K / V,B still work when GUI input is not capturing.");
}

} // namespace west::editor
