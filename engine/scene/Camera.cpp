// =============================================================================
// WestEngine - Scene
// Minimal camera with row-major view-projection output
// =============================================================================
#include "scene/Camera.h"

#include "core/Assert.h"

#include <cmath>

namespace west::scene
{

namespace
{

struct Vec3
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

[[nodiscard]] Vec3 ToVec3(const std::array<float, 3>& value)
{
    return {value[0], value[1], value[2]};
}

[[nodiscard]] Vec3 operator-(const Vec3& lhs, const Vec3& rhs)
{
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

[[nodiscard]] float Dot(const Vec3& lhs, const Vec3& rhs)
{
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] Vec3 Cross(const Vec3& lhs, const Vec3& rhs)
{
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] Vec3 Normalize(const Vec3& value)
{
    const float length = std::sqrt(Dot(value, value));
    WEST_ASSERT(length > 0.0f);
    return {value.x / length, value.y / length, value.z / length};
}

[[nodiscard]] std::array<float, 16> Multiply(const std::array<float, 16>& lhs, const std::array<float, 16>& rhs)
{
    std::array<float, 16> result{};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                sum += lhs[row * 4 + k] * rhs[k * 4 + column];
            }
            result[row * 4 + column] = sum;
        }
    }
    return result;
}

[[nodiscard]] std::array<float, 16> CreateLookAtRH(const std::array<float, 3>& eye,
                                                   const std::array<float, 3>& target,
                                                   const std::array<float, 3>& up)
{
    const Vec3 eyeVec = ToVec3(eye);
    const Vec3 targetVec = ToVec3(target);
    const Vec3 upVec = ToVec3(up);

    const Vec3 zAxis = Normalize(eyeVec - targetVec);
    const Vec3 xAxis = Normalize(Cross(upVec, zAxis));
    const Vec3 yAxis = Cross(zAxis, xAxis);

    return {
        xAxis.x, yAxis.x, zAxis.x, 0.0f,
        xAxis.y, yAxis.y, zAxis.y, 0.0f,
        xAxis.z, yAxis.z, zAxis.z, 0.0f,
        -Dot(xAxis, eyeVec), -Dot(yAxis, eyeVec), -Dot(zAxis, eyeVec), 1.0f,
    };
}

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
        xScale, 0.0f, 0.0f, 0.0f,
        0.0f, yScale, 0.0f, 0.0f,
        0.0f, 0.0f, zScale, -1.0f,
        0.0f, 0.0f, zTranslate, 0.0f,
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
    m_viewProjection = Multiply(m_view, m_projection);
}

} // namespace west::scene
