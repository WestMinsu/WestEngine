// =============================================================================
// WestEngine - Render
// Transient Render Graph resource pool
// =============================================================================
#pragma once

#include "render/RenderGraph/RenderGraphCompiler.h"

#include <memory>
#include <vector>

namespace west::rhi
{
class IRHIBuffer;
class IRHIDevice;
class IRHITexture;
} // namespace west::rhi

namespace west::render
{

class TransientResourcePool
{
public:
    void Prepare(rhi::IRHIDevice& device, const CompiledRenderGraph& compiledGraph);
    void Reset(rhi::IRHIDevice* device = nullptr);

    [[nodiscard]] rhi::IRHITexture* GetTexture(uint32_t resourceIndex) const;
    [[nodiscard]] rhi::IRHIBuffer* GetBuffer(uint32_t resourceIndex) const;

private:
    std::vector<std::unique_ptr<rhi::IRHITexture>> m_textures;
    std::vector<std::unique_ptr<rhi::IRHIBuffer>> m_buffers;
};

} // namespace west::render
