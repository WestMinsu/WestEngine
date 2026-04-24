// =============================================================================
// WestEngine - Scene
// Canonical glTF static-scene loader for Slice C
// =============================================================================
#pragma once

#include "scene/SceneAsset.h"

#include <filesystem>

namespace west::scene
{

struct SceneLoadOptions;

class MeshLoader final
{
public:
    [[nodiscard]] static SceneAsset LoadStaticSceneGltf(const std::filesystem::path& sourceFile,
                                                        const SceneLoadOptions& options);
};

} // namespace west::scene
