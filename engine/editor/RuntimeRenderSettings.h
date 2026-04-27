// =============================================================================
// WestEngine - Editor
// Runtime rendering controls shared by hotkeys and ImGui
// =============================================================================
#pragma once

#include "core/Types.h"
#include "render/Passes/BokehDOFPass.h"
#include "render/Passes/ToneMappingPass.h"

#include <array>

namespace west::editor
{

struct RuntimeRenderSettings
{
    bool texturesEnabled = true;
    bool shadowsEnabled = true;
    bool ssaoEnabled = true;
    bool iblEnabled = true;
    bool alphaDiscardEnabled = true;

    float lightIntensity = 5.2f;
    float lightElevationDegrees = 67.5f;
    float lightAzimuthDegrees = 35.5f;
    float environmentIntensity = 1.0f;
    float diffuseWeight = 1.0f;
    float specularWeight = 1.0f;
    float metallicScale = 1.0f;
    float roughnessScale = 1.0f;

    float ssaoRadius = 0.10f;
    float ssaoBias = 0.025f;
    int ssaoSampleCount = 16;
    float ssaoPower = 2.0f;

    float shadowBias = 0.0012f;
    float shadowNormalBias = 0.0125f;

    float cameraMoveSpeed = 12.0f;
    float cameraMouseSensitivity = 0.0035f;
    float cameraFovDegrees = 60.0f;
    float cameraNearPlane = 0.1f;
    float cameraFarPlane = 500.0f;

    uint32 postPresetIndex = 0;
    bool postPresetDirty = false;
    bool bokehEnabled = false;
    render::ToneMappingPass::PostSettings post{};
    render::BokehDOFPass::Settings bokeh{};

    bool imguiVisible = true;
    bool imguiCaptureInput = true;
    bool imguiWantsMouseCapture = false;
    bool imguiWantsKeyboardCapture = false;

    std::array<bool, 256> keyState{};
};

} // namespace west::editor
