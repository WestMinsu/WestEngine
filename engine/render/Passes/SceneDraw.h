// =============================================================================
// WestEngine - Render
// Shared static-scene draw item descriptions for passes
// =============================================================================
#pragma once

#include "render/RenderGraph/RenderGraphResource.h"

#include <array>
#include <cstdint>

namespace west::rhi
{
class IRHIBuffer;
} // namespace west::rhi

namespace west::render
{

struct StaticMeshDrawItem
{
    rhi::IRHIBuffer* vertexBuffer = nullptr;
    rhi::IRHIBuffer* indexBuffer = nullptr;
    BufferHandle vertexBufferHandle{};
    BufferHandle indexBufferHandle{};
    uint64_t vertexOffsetBytes = 0;
    uint64_t indexOffsetBytes = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
    std::array<float, 16> modelMatrix = {};
};

struct GPUSceneDrawRecord
{
    uint32_t materialIndex = 0;
    uint32_t indexCount = 0;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    std::array<float, 16> modelMatrix = {};
    std::array<float, 4> boundsSphere = {};
    std::array<float, 4> boundsMin = {};
    std::array<float, 4> boundsMax = {};
};

static_assert(sizeof(GPUSceneDrawRecord) == 128);

struct DrawIndexedIndirectArgs
{
    uint32_t indexCountPerInstance = 0;
    uint32_t instanceCount = 1;
    uint32_t firstIndex = 0;
    int32_t vertexOffset = 0;
    uint32_t firstInstance = 0;
};

static_assert(sizeof(DrawIndexedIndirectArgs) == 20);

} // namespace west::render
