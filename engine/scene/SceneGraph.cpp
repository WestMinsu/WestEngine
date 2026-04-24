// =============================================================================
// WestEngine - Scene
// Minimal demo-scene data model
// =============================================================================
#include "scene/SceneGraph.h"

#include <cmath>
#include <utility>

namespace west::scene
{

namespace
{

[[nodiscard]] std::array<float, 16> IdentityMatrix()
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
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

[[nodiscard]] std::array<float, 16> Translation(float x, float y, float z)
{
    auto matrix = IdentityMatrix();
    matrix[12] = x;
    matrix[13] = y;
    matrix[14] = z;
    return matrix;
}

[[nodiscard]] std::array<float, 16> Scale(float x, float y, float z)
{
    return {
        x,    0.0f, 0.0f, 0.0f,
        0.0f, y,    0.0f, 0.0f,
        0.0f, 0.0f, z,    0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] std::array<float, 16> RotationY(float radians)
{
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return {
         c, 0.0f, -s, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
         s, 0.0f,  c, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] MeshData CreatePlaneMesh(uint32_t materialIndex)
{
    MeshData mesh{};
    mesh.materialIndex = materialIndex;
    mesh.debugName = "Floor";
    mesh.vertices = {
        {{-1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},
        {{ 1.0f, 0.0f, -1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},
        {{ 1.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},
        {{-1.0f, 0.0f,  1.0f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},
    };
    mesh.indices = {0, 1, 2, 0, 2, 3};
    return mesh;
}

[[nodiscard]] MeshData CreateCubeMesh(uint32_t materialIndex, const char* debugName)
{
    MeshData mesh{};
    mesh.materialIndex = materialIndex;
    mesh.debugName = debugName;
    mesh.vertices = {
        {{-1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 1.0f}},
        {{ 1.0f, -1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 1.0f}},
        {{ 1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {1.0f, 0.0f}},
        {{-1.0f,  1.0f,  1.0f}, { 0.0f,  0.0f,  1.0f}, {0.0f, 0.0f}},

        {{ 1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 1.0f}},
        {{-1.0f, -1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 1.0f}},
        {{-1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {1.0f, 0.0f}},
        {{ 1.0f,  1.0f, -1.0f}, { 0.0f,  0.0f, -1.0f}, {0.0f, 0.0f}},

        {{-1.0f, -1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}},
        {{-1.0f, -1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}},
        {{-1.0f,  1.0f,  1.0f}, {-1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}},
        {{-1.0f,  1.0f, -1.0f}, {-1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}},

        {{ 1.0f, -1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 1.0f}},
        {{ 1.0f, -1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 1.0f}},
        {{ 1.0f,  1.0f, -1.0f}, { 1.0f,  0.0f,  0.0f}, {1.0f, 0.0f}},
        {{ 1.0f,  1.0f,  1.0f}, { 1.0f,  0.0f,  0.0f}, {0.0f, 0.0f}},

        {{-1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 1.0f}},
        {{ 1.0f,  1.0f,  1.0f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 1.0f}},
        {{ 1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, {1.0f, 0.0f}},
        {{-1.0f,  1.0f, -1.0f}, { 0.0f,  1.0f,  0.0f}, {0.0f, 0.0f}},

        {{-1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 1.0f}},
        {{ 1.0f, -1.0f, -1.0f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 1.0f}},
        {{ 1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, {1.0f, 0.0f}},
        {{-1.0f, -1.0f,  1.0f}, { 0.0f, -1.0f,  0.0f}, {0.0f, 0.0f}},
    };

    mesh.indices = {
         0,  1,  2,  0,  2,  3,
         4,  5,  6,  4,  6,  7,
         8,  9, 10,  8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23,
    };

    return mesh;
}

} // namespace

SceneGraph SceneGraph::CreateDemoScene()
{
    SceneGraph scene{};

    Material floorMaterial{};
    floorMaterial.baseColor = {0.10f, 0.12f, 0.16f, 1.0f};
    floorMaterial.roughness = 0.95f;
    floorMaterial.debugName = "FloorMaterial";

    Material warmCubeMaterial{};
    warmCubeMaterial.baseColor = {0.80f, 0.30f, 0.20f, 1.0f};
    warmCubeMaterial.roughness = 0.35f;
    warmCubeMaterial.debugName = "WarmCube";

    Material metalCubeMaterial{};
    metalCubeMaterial.baseColor = {0.75f, 0.78f, 0.82f, 1.0f};
    metalCubeMaterial.roughness = 0.20f;
    metalCubeMaterial.metallic = 1.0f;
    metalCubeMaterial.debugName = "MetalCube";

    scene.m_materials = {std::move(floorMaterial), std::move(warmCubeMaterial), std::move(metalCubeMaterial)};

    scene.m_meshes.push_back(CreatePlaneMesh(0));
    scene.m_meshes.push_back(CreateCubeMesh(1, "WarmCubeMesh"));
    scene.m_meshes.push_back(CreateCubeMesh(2, "MetalCubeMesh"));

    scene.m_instances.push_back({
        0,
        Multiply(Scale(6.0f, 1.0f, 6.0f), Translation(0.0f, -1.0f, 0.0f)),
    });
    scene.m_instances.push_back({
        1,
        Multiply(RotationY(0.45f), Translation(-1.5f, 0.0f, 0.0f)),
    });
    scene.m_instances.push_back({
        2,
        Multiply(Multiply(Scale(0.75f, 1.5f, 0.75f), RotationY(-0.6f)), Translation(1.75f, -0.25f, 0.8f)),
    });

    return scene;
}

} // namespace west::scene
