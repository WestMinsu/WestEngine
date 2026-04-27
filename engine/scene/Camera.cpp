// =============================================================================
// WestEngine - Scene
// Minimal camera with row-major view-projection output
// =============================================================================
#include "scene/Camera.h"

#include "core/Assert.h"
#include "scene/SceneMath.h"

#include <cmath>

namespace west::scene
{

namespace
{

[[nodiscard]] std::array<float, 16> CreatePerspectiveFovRH(float fovYRadians, float aspectRatio, float nearPlane,
                                                           float farPlane)
{
    const float tanHalfFov = std::tan(fovYRadians * 0.5f);
    WEST_ASSERT(tanHalfFov > 0.0f);
    WEST_ASSERT(aspectRatio > 0.0f);
    WEST_ASSERT(farPlane > nearPlane);

    const float yScale = 1.0f / tanHalfFov;
    const float xScale = yScale / aspectRatio;
    const float zScale = farPlane / (nearPlane - farPlane);
    const float zTranslate = (nearPlane * farPlane) / (nearPlane - farPlane);

    return {
        xScale, 0.0f, 0.0f, 0.0f, 0.0f, yScale, 0.0f, 0.0f, 0.0f, 0.0f, zScale, -1.0f, 0.0f, 0.0f, zTranslate, 0.0f,
    };
}

} // namespace

void Camera::SetPerspective(float fovYRadians, float aspectRatio, float nearPlane, float farPlane)
{
    m_projection = CreatePerspectiveFovRH(fovYRadians, aspectRatio, nearPlane, farPlane);
    UpdateViewProjection();
}

void Camera::SetLookAt(const std::array<float, 3>& eye, const std::array<float, 3>& target,
                       const std::array<float, 3>& up)
{
    m_eye = eye;
    m_target = target;
    m_up = up;
    m_view = CreateLookAtRH(eye, target, up);
    UpdateViewProjection();
}

void Camera::UpdateViewProjection()
{
    m_viewProjection = MultiplyMatrix4x4(m_view, m_projection);
}

} // namespace west::scene
