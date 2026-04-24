// =============================================================================
// WestEngine - Render
// Transient Render Graph resource pool
// =============================================================================
#include "render/RenderGraph/TransientResourcePool.h"

#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHITexture.h"

#include <utility>

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
            std::unique_ptr<rhi::IRHITexture> retiredTexture = std::move(m_textures[resourceIndex]);
            if (retiredTexture && retiredTexture->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
            {
                device.UnregisterBindlessResource(retiredTexture->GetBindlessIndex());
            }

            if (retiredTexture)
            {
                const uint64_t fenceValue = device.GetCurrentFrameFenceValue();
                rhi::IRHITexture* texture = retiredTexture.release();
                device.EnqueueDeferredDeletion([texture]()
                {
                    delete texture;
                }, fenceValue);
            }
            m_textureAliasSlots[resourceIndex] = kInvalidRenderGraphIndex;
        };

        const auto releaseBuffer = [&]()
        {
            std::unique_ptr<rhi::IRHIBuffer> retiredBuffer = std::move(m_buffers[resourceIndex]);
            if (retiredBuffer && retiredBuffer->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
            {
                device.UnregisterBindlessResource(retiredBuffer->GetBindlessIndex());
            }

            if (retiredBuffer)
            {
                const uint64_t fenceValue = device.GetCurrentFrameFenceValue();
                rhi::IRHIBuffer* buffer = retiredBuffer.release();
                device.EnqueueDeferredDeletion([buffer]()
                {
                    delete buffer;
                }, fenceValue);
            }
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
            if (m_buffers[resourceIndex] &&
                m_buffers[resourceIndex]->GetBindlessIndex() == rhi::kInvalidBindlessIndex &&
                (HasFlag(resource.bufferDesc.usage, rhi::RHIBufferUsage::ConstantBuffer) ||
                 HasFlag(resource.bufferDesc.usage, rhi::RHIBufferUsage::StorageBuffer)))
            {
                const bool writable = HasFlag(resource.bufferDesc.usage, rhi::RHIBufferUsage::StorageBuffer);
                device.RegisterBindlessResource(m_buffers[resourceIndex].get(), writable);
            }
        }
    }
}

void TransientResourcePool::Reset(rhi::IRHIDevice* device)
{
    if (device)
    {
        const uint64_t fenceValue = device->GetCurrentFrameFenceValue();
        for (std::unique_ptr<rhi::IRHITexture>& texture : m_textures)
        {
            if (texture && texture->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
            {
                device->UnregisterBindlessResource(texture->GetBindlessIndex());
            }
            if (texture)
            {
                rhi::IRHITexture* retiredTexture = texture.release();
                device->EnqueueDeferredDeletion([retiredTexture]()
                {
                    delete retiredTexture;
                }, fenceValue);
            }
        }

        for (std::unique_ptr<rhi::IRHIBuffer>& buffer : m_buffers)
        {
            if (buffer && buffer->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
            {
                device->UnregisterBindlessResource(buffer->GetBindlessIndex());
            }
            if (buffer)
            {
                rhi::IRHIBuffer* retiredBuffer = buffer.release();
                device->EnqueueDeferredDeletion([retiredBuffer]()
                {
                    delete retiredBuffer;
                }, fenceValue);
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
