// =============================================================================
// WestEngine - Scene
// Minimal demo-scene data model
// =============================================================================
#pragma once

#include "scene/SceneAsset.h"

namespace west::scene
{

class SceneGraph final
{
public:
    static SceneGraph CreateDemoScene();

    [[nodiscard]] const std::vector<MeshData>& GetMeshes() const
    {
        return m_meshes;
    }

    [[nodiscard]] const std::vector<Material>& GetMaterials() const
    {
        return m_materials;
    }

    [[nodiscard]] const std::vector<InstanceData>& GetInstances() const
    {
        return m_instances;
    }

private:
    std::vector<MeshData> m_meshes;
    std::vector<Material> m_materials;
    std::vector<InstanceData> m_instances;
};

} // namespace west::scene
