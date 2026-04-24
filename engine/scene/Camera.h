// =============================================================================
// WestEngine - Scene
// Minimal camera with row-major view-projection output
// =============================================================================
#pragma once

#include <array>

namespace west::scene
{

class Camera final
{
public:
    void SetPerspective(float fovYRadians, float aspectRatio, float nearPlane, float farPlane);
    void SetLookAt(const std::array<float, 3>& eye, const std::array<float, 3>& target,
                   const std::array<float, 3>& up);

    [[nodiscard]] const std::array<float, 16>& GetViewProjectionMatrix() const
    {
        return m_viewProjection;
    }

    [[nodiscard]] const std::array<float, 3>& GetPosition() const
    {
        return m_eye;
    }

private:
    void UpdateViewProjection();

    std::array<float, 3> m_eye = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> m_target = {0.0f, 0.0f, 1.0f};
    std::array<float, 3> m_up = {0.0f, 1.0f, 0.0f};
    std::array<float, 16> m_view = {};
    std::array<float, 16> m_projection = {};
    std::array<float, 16> m_viewProjection = {};
};

} // namespace west::scene
