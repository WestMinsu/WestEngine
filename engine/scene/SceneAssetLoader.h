// =============================================================================
// WestEngine - Scene
// Scene asset loading entry points for rendering
// =============================================================================
#pragma once

#include "scene/SceneAsset.h"

#include <filesystem>

namespace west::scene
{

struct SceneLoadOptions
{
    float uniformScale = 1.0f;
    bool enableCache = true;
    bool enableStaticMeshMerge = true;
};

class SceneAssetLoader final
{
public:
    [[nodiscard]] static SceneAsset LoadStaticScene(const std::filesystem::path& sourceFile,
                                                    const SceneLoadOptions& options = {});
    [[nodiscard]] static SceneAsset LoadStaticScene(const std::filesystem::path& sourceFile, float uniformScale);
    [[nodiscard]] static SceneAsset LoadAmazonLumberyardBistro(const std::filesystem::path& assetRoot,
                                                               const SceneLoadOptions& options = {});
};

} // namespace west::scene
