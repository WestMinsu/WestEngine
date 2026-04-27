// =============================================================================
// WestEngine - Scene
// Free-look camera pose and movement controller
// =============================================================================
#pragma once

#include <array>

namespace west::scene
{

class Camera;

class FreeLookCameraController final
{
public:
    static constexpr float kMinPitchRadians = -1.45f;
    static constexpr float kMaxPitchRadians = 1.45f;

    [[nodiscard]] float ResetToBounds(const std::array<float, 3>& boundsMin, const std::array<float, 3>& boundsMax);
    void SetPose(const std::array<float, 3>& position, float yawRadians, float pitchRadians);
    void SetPosition(const std::array<float, 3>& position);
    void SetYawRadians(float yawRadians);
    void SetPitchRadians(float pitchRadians);
    void Rotate(float yawDeltaRadians, float pitchDeltaRadians);
    void MoveFlat(float forwardUnits, float rightUnits, float verticalUnits);
    void ApplyToCamera(Camera& camera) const;

    [[nodiscard]] const std::array<float, 3>& GetPosition() const
    {
        return m_position;
    }

    [[nodiscard]] float GetYawRadians() const
    {
        return m_yawRadians;
    }

    [[nodiscard]] float GetPitchRadians() const
    {
        return m_pitchRadians;
    }

    [[nodiscard]] std::array<float, 3> GetForward() const;
    [[nodiscard]] std::array<float, 3> GetRight() const;
    [[nodiscard]] std::array<float, 3> GetUp() const;
    [[nodiscard]] std::array<float, 3> GetTarget() const;

private:
    std::array<float, 3> m_position = {0.0f, 0.0f, 0.0f};
    float m_yawRadians = 0.0f;
    float m_pitchRadians = 0.0f;
};

} // namespace west::scene
