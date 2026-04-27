// =============================================================================
// WestEngine - Scene
// Small row-major math helpers shared by scene and runtime code
// =============================================================================
#pragma once

#include "core/Assert.h"

#include <array>
#include <cmath>

namespace west::scene
{

[[nodiscard]] inline float Length3(const std::array<float, 3>& value)
{
    return std::sqrt((value[0] * value[0]) + (value[1] * value[1]) + (value[2] * value[2]));
}

[[nodiscard]] inline float Dot3(const std::array<float, 3>& lhs, const std::array<float, 3>& rhs)
{
    return (lhs[0] * rhs[0]) + (lhs[1] * rhs[1]) + (lhs[2] * rhs[2]);
}

[[nodiscard]] inline std::array<float, 3> Cross3(const std::array<float, 3>& lhs, const std::array<float, 3>& rhs)
{
    return {
        (lhs[1] * rhs[2]) - (lhs[2] * rhs[1]),
        (lhs[2] * rhs[0]) - (lhs[0] * rhs[2]),
        (lhs[0] * rhs[1]) - (lhs[1] * rhs[0]),
    };
}

[[nodiscard]] inline std::array<float, 3> Normalize3(const std::array<float, 3>& value,
                                                     const std::array<float, 3>& fallback = {0.0f, 0.0f, 1.0f})
{
    const float length = Length3(value);
    if (length <= 0.0001f)
    {
        return fallback;
    }

    return {
        value[0] / length,
        value[1] / length,
        value[2] / length,
    };
}

[[nodiscard]] inline std::array<float, 3> Normalize3Checked(const std::array<float, 3>& value)
{
    const float length = Length3(value);
    WEST_ASSERT(length > 0.0f);
    return {
        value[0] / length,
        value[1] / length,
        value[2] / length,
    };
}

[[nodiscard]] inline std::array<float, 16> MultiplyMatrix4x4(const std::array<float, 16>& lhs,
                                                             const std::array<float, 16>& rhs)
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

[[nodiscard]] inline std::array<float, 3> TransformPointAffine(const std::array<float, 16>& matrix,
                                                               const float position[3])
{
    return {
        (position[0] * matrix[0]) + (position[1] * matrix[4]) + (position[2] * matrix[8]) + matrix[12],
        (position[0] * matrix[1]) + (position[1] * matrix[5]) + (position[2] * matrix[9]) + matrix[13],
        (position[0] * matrix[2]) + (position[1] * matrix[6]) + (position[2] * matrix[10]) + matrix[14],
    };
}

[[nodiscard]] inline std::array<float, 3> TransformPointHomogeneous(const std::array<float, 16>& matrix,
                                                                    const std::array<float, 3>& point)
{
    const float x = point[0];
    const float y = point[1];
    const float z = point[2];

    const float transformedX = (x * matrix[0]) + (y * matrix[4]) + (z * matrix[8]) + matrix[12];
    const float transformedY = (x * matrix[1]) + (y * matrix[5]) + (z * matrix[9]) + matrix[13];
    const float transformedZ = (x * matrix[2]) + (y * matrix[6]) + (z * matrix[10]) + matrix[14];
    const float transformedW = (x * matrix[3]) + (y * matrix[7]) + (z * matrix[11]) + matrix[15];

    if (std::abs(transformedW) > 0.0001f)
    {
        return {
            transformedX / transformedW,
            transformedY / transformedW,
            transformedZ / transformedW,
        };
    }

    return {transformedX, transformedY, transformedZ};
}

[[nodiscard]] inline std::array<float, 16> CreateLookAtRH(const std::array<float, 3>& eye,
                                                          const std::array<float, 3>& target,
                                                          const std::array<float, 3>& up)
{
    const std::array<float, 3> zAxis = Normalize3Checked({
        eye[0] - target[0],
        eye[1] - target[1],
        eye[2] - target[2],
    });
    const std::array<float, 3> xAxis = Normalize3Checked(Cross3(up, zAxis));
    const std::array<float, 3> yAxis = Cross3(zAxis, xAxis);

    return {
        xAxis[0], yAxis[0], zAxis[0], 0.0f, xAxis[1],          yAxis[1],          zAxis[1],          0.0f,
        xAxis[2], yAxis[2], zAxis[2], 0.0f, -Dot3(xAxis, eye), -Dot3(yAxis, eye), -Dot3(zAxis, eye), 1.0f,
    };
}

} // namespace west::scene
