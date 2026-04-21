// =============================================================================
// WestEngine - Render
// Transient Render Graph resource pool
// =============================================================================
#include "render/RenderGraph/TransientResourcePool.h"

#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHITexture.h"

namespace west::render
{

namespace
{

[[nodiscard]] bool SameTextureDesc(const rhi::RHITextureDesc& lhs, const rhi::RHITextureDesc& rhs)
{
    return lhs.width == rhs.width &&
           lhs.height == rhs.height &&
           lhs.depth == rhs.depth &&
           lhs.mipLevels == rhs.mipLevels &&
           lhs.arrayLayers == rhs.arrayLayers &&
           lhs.format == rhs.format &&
           lhs.usage == rhs.usage &&
           lhs.dimension == rhs.dimension;
}

[[nodiscard]] bool SameBufferDesc(const rhi::RHIBufferDesc& lhs, const rhi::RHIBufferDesc& rhs)
{
    return lhs.sizeBytes == rhs.sizeBytes &&
           lhs.structureByteStride == rhs.structureByteStride &&
           lhs.usage == rhs.usage &&
           lhs.memoryType == rhs.memoryType;
}

} // namespace

void TransientResourcePool::Prepare(rhi::IRHIDevice& device, const CompiledRenderGraph& compiledGraph)
{
    m_textures.resize(compiledGraph.resources.size());
    m_buffers.resize(compiledGraph.resources.size());

    for (uint32_t resourceIndex = 0; resourceIndex < compiledGraph.resources.size(); ++resourceIndex)
    {
        const RenderGraphResourceInfo& resource = compiledGraph.resources[resourceIndex];
        if (resource.imported)
        {
            continue;
        }

        if (resource.kind == ResourceKind::Texture)
        {
            if (m_textures[resourceIndex] && SameTextureDesc(m_textures[resourceIndex]->GetDesc(), resource.textureDesc))
            {
                continue;
            }

            if (m_textures[resourceIndex] &&
                m_textures[resourceIndex]->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
            {
                device.UnregisterBindlessResource(m_textures[resourceIndex]->GetBindlessIndex());
            }

            m_textures[resourceIndex] = device.CreateTransientTexture(resource.textureDesc, resource.alias.slot);
            if (m_textures[resourceIndex] &&
                m_textures[resourceIndex]->GetBindlessIndex() == rhi::kInvalidBindlessIndex &&
                HasFlag(resource.textureDesc.usage, rhi::RHITextureUsage::ShaderResource))
            {
                device.RegisterBindlessResource(m_textures[resourceIndex].get());
            }
        }
        else
        {
            if (m_buffers[resourceIndex] && SameBufferDesc(m_buffers[resourceIndex]->GetDesc(), resource.bufferDesc))
            {
                continue;
            }

            m_buffers[resourceIndex] = device.CreateTransientBuffer(resource.bufferDesc, resource.alias.slot);
        }
    }
}

void TransientResourcePool::Reset(rhi::IRHIDevice* device)
{
    if (device)
    {
        for (const std::unique_ptr<rhi::IRHITexture>& texture : m_textures)
        {
            if (texture && texture->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
            {
                device->UnregisterBindlessResource(texture->GetBindlessIndex());
            }
        }
    }

    m_textures.clear();
    m_buffers.clear();
}

rhi::IRHITexture* TransientResourcePool::GetTexture(uint32_t resourceIndex) const
{
    if (resourceIndex >= m_textures.size())
    {
        return nullptr;
    }

    return m_textures[resourceIndex].get();
}

rhi::IRHIBuffer* TransientResourcePool::GetBuffer(uint32_t resourceIndex) const
{
    if (resourceIndex >= m_buffers.size())
    {
        return nullptr;
    }

    return m_buffers[resourceIndex].get();
}

} // namespace west::render
