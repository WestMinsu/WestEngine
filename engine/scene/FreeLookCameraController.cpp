// =============================================================================
// WestEngine - Scene
// Free-look camera pose and movement controller
// =============================================================================
#include "scene/FreeLookCameraController.h"

#include "scene/Camera.h"
#include "scene/SceneMath.h"

#include <algorithm>
#include <cmath>

namespace west::scene
{

namespace
{

[[nodiscard]] std::array<float, 3> MakeForwardVector(float yawRadians, float pitchRadians)
{
    const float cosPitch = std::cos(pitchRadians);
    return Normalize3({
        cosPitch * std::cos(yawRadians),
        std::sin(pitchRadians),
        cosPitch * std::sin(yawRadians),
    });
}

[[nodiscard]] std::array<float, 3> MakeFlatForwardVector(float yawRadians)
{
    return Normalize3({
        std::cos(yawRadians),
        0.0f,
        std::sin(yawRadians),
    });
}

[[nodiscard]] std::array<float, 3> MakeRightVector(float yawRadians)
{
    return Normalize3({
        -std::sin(yawRadians),
        0.0f,
        std::cos(yawRadians),
    });
}

} // namespace

float FreeLookCameraController::ResetToBounds(const std::array<float, 3>& boundsMin,
                                              const std::array<float, 3>& boundsMax)
{
    const std::array<float, 3> sceneCenter = {
        (boundsMin[0] + boundsMax[0]) * 0.5f,
        (boundsMin[1] + boundsMax[1]) * 0.5f,
        (boundsMin[2] + boundsMax[2]) * 0.5f,
    };
    const std::array<float, 3> extents = {
        boundsMax[0] - boundsMin[0],
        boundsMax[1] - boundsMin[1],
        boundsMax[2] - boundsMin[2],
    };

    const float sceneRadius = std::max({extents[0], extents[1], extents[2]}) * 0.5f;
    m_position = {
        sceneCenter[0] + (sceneRadius * 0.45f),
        boundsMin[1] + (sceneRadius * 0.18f),
        sceneCenter[2] + (sceneRadius * 0.95f),
    };

    const std::array<float, 3> toCenter = Normalize3({
        sceneCenter[0] - m_position[0],
        sceneCenter[1] - m_position[1],
        sceneCenter[2] - m_position[2],
    });

    m_yawRadians = std::atan2(toCenter[2], toCenter[0]);
    m_pitchRadians = std::asin(std::clamp(toCenter[1], -0.95f, 0.95f));
    return sceneRadius;
}

void FreeLookCameraController::SetPose(const std::array<float, 3>& position, float yawRadians, float pitchRadians)
{
    m_position = position;
    m_yawRadians = yawRadians;
    SetPitchRadians(pitchRadians);
}

void FreeLookCameraController::SetPosition(const std::array<float, 3>& position)
{
    m_position = position;
}

void FreeLookCameraController::SetYawRadians(float yawRadians)
{
    m_yawRadians = yawRadians;
}

void FreeLookCameraController::SetPitchRadians(float pitchRadians)
{
    m_pitchRadians = std::clamp(pitchRadians, kMinPitchRadians, kMaxPitchRadians);
}

void FreeLookCameraController::Rotate(float yawDeltaRadians, float pitchDeltaRadians)
{
    m_yawRadians += yawDeltaRadians;
    SetPitchRadians(m_pitchRadians + pitchDeltaRadians);
}

void FreeLookCameraController::MoveFlat(float forwardUnits, float rightUnits, float verticalUnits)
{
    const std::array<float, 3> flatForward = MakeFlatForwardVector(m_yawRadians);
    const std::array<float, 3> right = MakeRightVector(m_yawRadians);

    m_position[0] += (flatForward[0] * forwardUnits) + (right[0] * rightUnits);
    m_position[1] += verticalUnits;
    m_position[2] += (flatForward[2] * forwardUnits) + (right[2] * rightUnits);
}

void FreeLookCameraController::ApplyToCamera(Camera& camera) const
{
    camera.SetLookAt(m_position, GetTarget(), {0.0f, 1.0f, 0.0f});
}

std::array<float, 3> FreeLookCameraController::GetForward() const
{
    return MakeForwardVector(m_yawRadians, m_pitchRadians);
}

std::array<float, 3> FreeLookCameraController::GetRight() const
{
    return MakeRightVector(m_yawRadians);
}

std::array<float, 3> FreeLookCameraController::GetUp() const
{
    return Normalize3(Cross3(GetRight(), GetForward()));
}

std::array<float, 3> FreeLookCameraController::GetTarget() const
{
    const std::array<float, 3> forward = GetForward();
    return {
        m_position[0] + forward[0],
        m_position[1] + forward[1],
        m_position[2] + forward[2],
    };
}

} // namespace west::scene
