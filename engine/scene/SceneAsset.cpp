// =============================================================================
// WestEngine - Scene
// Static scene asset containers for rendering bring-up
// =============================================================================
#include "scene/SceneAsset.h"

namespace west::scene
{

void SceneAsset::SetDebugName(std::string debugName)
{
    m_debugName = std::move(debugName);
}

void SceneAsset::SetBounds(std::array<float, 3> boundsMin, std::array<float, 3> boundsMax)
{
    m_boundsMin = boundsMin;
    m_boundsMax = boundsMax;
}

void SceneAsset::SetLoadStats(SceneLoadStats loadStats)
{
    m_loadStats = std::move(loadStats);
}

uint32_t SceneAsset::AddTexture(TextureAsset texture)
{
    m_textures.push_back(std::move(texture));
    return static_cast<uint32_t>(m_textures.size() - 1);
}

uint32_t SceneAsset::AddMaterial(Material material)
{
    m_materials.push_back(std::move(material));
    return static_cast<uint32_t>(m_materials.size() - 1);
}

uint32_t SceneAsset::AddMesh(MeshData mesh)
{
    m_meshes.push_back(std::move(mesh));
    return static_cast<uint32_t>(m_meshes.size() - 1);
}

void SceneAsset::AddInstance(InstanceData instance)
{
    m_instances.push_back(std::move(instance));
}

void SceneAsset::ReplaceGeometry(std::vector<MeshData> meshes, std::vector<InstanceData> instances)
{
    m_meshes = std::move(meshes);
    m_instances = std::move(instances);
}

} // namespace west::scene
