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

void TransientResourcePool::Prepare(rhi::IRHIDevice& device, const CompiledRenderGraph& compiledGraph)
{
    m_textures.resize(compiledGraph.resources.size());
    m_buffers.resize(compiledGraph.resources.size());
    m_textureAliasSlots.resize(compiledGraph.resources.size(), kInvalidRenderGraphIndex);
    m_bufferAliasSlots.resize(compiledGraph.resources.size(), kInvalidRenderGraphIndex);

    for (uint32_t resourceIndex = 0; resourceIndex < compiledGraph.resources.size(); ++resourceIndex)
    {
        const RenderGraphResourceInfo& resource = compiledGraph.resources[resourceIndex];
        const auto releaseTexture = [&]()
        {
            if (m_textures[resourceIndex] &&
                m_textures[resourceIndex]->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
            {
                device.UnregisterBindlessResource(m_textures[resourceIndex]->GetBindlessIndex());
            }

            m_textures[resourceIndex].reset();
            m_textureAliasSlots[resourceIndex] = kInvalidRenderGraphIndex;
        };

        const auto releaseBuffer = [&]()
        {
            m_buffers[resourceIndex].reset();
            m_bufferAliasSlots[resourceIndex] = kInvalidRenderGraphIndex;
        };

        if (resource.imported || !resource.lifetime.IsValid())
        {
            releaseTexture();
            releaseBuffer();
            continue;
        }

        if (resource.kind == ResourceKind::Texture)
        {
            releaseBuffer();
            if (m_textures[resourceIndex] &&
                m_textures[resourceIndex]->GetDesc() == resource.textureDesc &&
                m_textureAliasSlots[resourceIndex] == resource.alias.slot)
            {
                continue;
            }

            releaseTexture();
            m_textures[resourceIndex] = device.CreateTransientTexture(resource.textureDesc, resource.alias.slot);
            m_textureAliasSlots[resourceIndex] = resource.alias.slot;
            if (m_textures[resourceIndex] &&
                m_textures[resourceIndex]->GetBindlessIndex() == rhi::kInvalidBindlessIndex &&
                HasFlag(resource.textureDesc.usage, rhi::RHITextureUsage::ShaderResource))
            {
                device.RegisterBindlessResource(m_textures[resourceIndex].get());
            }
        }
        else
        {
            releaseTexture();
            if (m_buffers[resourceIndex] &&
                m_buffers[resourceIndex]->GetDesc() == resource.bufferDesc &&
                m_bufferAliasSlots[resourceIndex] == resource.alias.slot)
            {
                continue;
            }

            releaseBuffer();
            m_buffers[resourceIndex] = device.CreateTransientBuffer(resource.bufferDesc, resource.alias.slot);
            m_bufferAliasSlots[resourceIndex] = resource.alias.slot;
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
    m_textureAliasSlots.clear();
    m_bufferAliasSlots.clear();
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
