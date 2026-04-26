// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan texture — VMA backed image or non-owning swapchain image
// =============================================================================
#pragma once

#include "rhi/interface/IRHITexture.h"
#include "rhi/vulkan/VulkanHelpers.h"

#include <memory>
#include <vk_mem_alloc.h>

namespace west::rhi
{

class VulkanDevice;

class VulkanTexture final : public IRHITexture
{
public:
    VulkanTexture() = default;
    ~VulkanTexture() override;
    VulkanTexture(const VulkanTexture&) = delete;
    VulkanTexture& operator=(const VulkanTexture&) = delete;
    VulkanTexture(VulkanTexture&& other) noexcept;
    VulkanTexture& operator=(VulkanTexture&&) = delete;

    void Initialize(VulkanDevice* device, const RHITextureDesc& desc);
    void InitializeAliased(VulkanDevice* device, const RHITextureDesc& desc,
                           std::shared_ptr<VmaAllocation_T> aliasingAllocation);

    /// Initialize from an existing VkImage (e.g. swapchain image).
    /// The texture does NOT own the image in this case.
    void InitFromSwapChain(VkImage image, VkImageView imageView, const RHITextureDesc& desc);

    // ── IRHITexture interface ─────────────────────────────────────────
    const RHITextureDesc& GetDesc() const override
    {
        return m_desc;
    }
    BindlessIndex GetBindlessIndex() const override
    {
        return m_bindlessIndex;
    }

    // ── Internal ──────────────────────────────────────────────────────
    VkImage GetVkImage() const
    {
        return m_image;
    }
    VkImageView GetVkImageView() const
    {
        return m_imageView;
    }
    void SetBindlessIndex(BindlessIndex index)
    {
        m_bindlessIndex = index;
    }

private:
    VmaAllocator m_vmaAllocator = VK_NULL_HANDLE;
    VmaAllocation m_allocation = VK_NULL_HANDLE;
    std::shared_ptr<VmaAllocation_T> m_aliasingAllocation;
    VkImage m_image = VK_NULL_HANDLE; // Owned by allocation, non-owning for swapchain
    VkImageView m_imageView = VK_NULL_HANDLE;
    RHITextureDesc m_desc{};
    BindlessIndex m_bindlessIndex = kInvalidBindlessIndex;
    VulkanDevice* m_device = nullptr;
    bool m_ownsImage = false;
    bool m_isAliased = false;
};

} // namespace west::rhi
