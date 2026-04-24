// =============================================================================
// WestEngine - Scene
// PBR material description for scene data
// =============================================================================
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string>

namespace west::scene
{

inline constexpr uint32_t kInvalidSceneTextureIndex = std::numeric_limits<uint32_t>::max();

struct Material
{
    std::array<float, 4> baseColor = {1.0f, 1.0f, 1.0f, 1.0f};
    uint32_t baseColorTextureIndex = kInvalidSceneTextureIndex;
    uint32_t opacityTextureIndex = kInvalidSceneTextureIndex;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float alphaCutoff = 0.0f;
    std::string debugName;
};

} // namespace west::scene
