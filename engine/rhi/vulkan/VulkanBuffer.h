// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan buffer — VMA backed buffer resource
// =============================================================================
#pragma once

#include "rhi/interface/IRHIBuffer.h"
#include "rhi/vulkan/VulkanHelpers.h"

#include <vk_mem_alloc.h>

namespace west::rhi
{

class VulkanMemoryAllocator;
class VulkanDevice;

class VulkanBuffer final : public IRHIBuffer
{
public:
    VulkanBuffer() = default;
    ~VulkanBuffer() override;
    VulkanBuffer(const VulkanBuffer&) = delete;
    VulkanBuffer& operator=(const VulkanBuffer&) = delete;
    VulkanBuffer(VulkanBuffer&&) = delete;
    VulkanBuffer& operator=(VulkanBuffer&&) = delete;

    void Initialize(VulkanDevice* device, const RHIBufferDesc& desc);

    // ── IRHIBuffer interface ──────────────────────────────────────────
    const RHIBufferDesc& GetDesc() const override { return m_desc; }
    void* Map() override;
    void Unmap() override;
    BindlessIndex GetBindlessIndex() const override { return m_bindlessIndex; }

    // ── Internal ──────────────────────────────────────────────────────
    VkBuffer GetVkBuffer() const { return m_buffer; }
    void SetBindlessIndex(BindlessIndex index) { m_bindlessIndex = index; }

private:
    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    RHIBufferDesc m_desc{};
    BindlessIndex m_bindlessIndex = kInvalidBindlessIndex;
    void* m_mappedPtr = nullptr;
    VulkanDevice* m_device = nullptr;
};

} // namespace west::rhi
