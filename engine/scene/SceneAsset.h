// =============================================================================
// WestEngine - Scene
// Static scene asset containers for rendering bring-up
// =============================================================================
#pragma once

#include "scene/Material.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace west::scene
{

struct MeshVertex
{
    float position[3] = {};
    float normal[3] = {};
    float uv[2] = {};
};

struct MeshData
{
    std::vector<MeshVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t materialIndex = 0;
    std::string debugName;
};

struct InstanceData
{
    uint32_t meshIndex = 0;
    std::array<float, 16> modelMatrix = {};
};

struct TextureAsset
{
    std::filesystem::path sourcePath;
    std::string debugName;
};

struct SceneLoadStats
{
    bool usedCache = false;
    bool cacheWritten = false;
    bool appliedMeshMerge = false;
    uint32_t sourceMeshCount = 0;
    uint32_t sourceInstanceCount = 0;
    uint64_t sourceVertexCount = 0;
    uint64_t sourceIndexCount = 0;
    uint32_t optimizedMeshCount = 0;
    uint32_t optimizedInstanceCount = 0;
    uint64_t optimizedVertexCount = 0;
    uint64_t optimizedIndexCount = 0;
    double cacheReadMs = 0.0;
    double importMs = 0.0;
    double optimizeMs = 0.0;
    double cacheWriteMs = 0.0;
    double totalLoadMs = 0.0;
};

class SceneAsset final
{
public:
    void SetDebugName(std::string debugName);
    void SetBounds(std::array<float, 3> boundsMin, std::array<float, 3> boundsMax);
    void SetLoadStats(SceneLoadStats loadStats);

    uint32_t AddTexture(TextureAsset texture);
    uint32_t AddMaterial(Material material);
    uint32_t AddMesh(MeshData mesh);
    void AddInstance(InstanceData instance);
    void ReplaceGeometry(std::vector<MeshData> meshes, std::vector<InstanceData> instances);

    [[nodiscard]] const std::string& GetDebugName() const
    {
        return m_debugName;
    }

    [[nodiscard]] const std::vector<TextureAsset>& GetTextures() const
    {
        return m_textures;
    }

    [[nodiscard]] const std::vector<Material>& GetMaterials() const
    {
        return m_materials;
    }

    [[nodiscard]] const std::vector<MeshData>& GetMeshes() const
    {
        return m_meshes;
    }

    [[nodiscard]] const std::vector<InstanceData>& GetInstances() const
    {
        return m_instances;
    }

    [[nodiscard]] const std::array<float, 3>& GetBoundsMin() const
    {
        return m_boundsMin;
    }

    [[nodiscard]] const std::array<float, 3>& GetBoundsMax() const
    {
        return m_boundsMax;
    }

    [[nodiscard]] const SceneLoadStats& GetLoadStats() const
    {
        return m_loadStats;
    }

private:
    std::string m_debugName;
    std::vector<TextureAsset> m_textures;
    std::vector<Material> m_materials;
    std::vector<MeshData> m_meshes;
    std::vector<InstanceData> m_instances;
    std::array<float, 3> m_boundsMin = {0.0f, 0.0f, 0.0f};
    std::array<float, 3> m_boundsMax = {0.0f, 0.0f, 0.0f};
    SceneLoadStats m_loadStats{};
};

} // namespace west::scene
