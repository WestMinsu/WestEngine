// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan texture — minimal wrapper for SwapChain images (Phase 1)
// =============================================================================
#pragma once

#include "rhi/interface/IRHITexture.h"
#include "rhi/vulkan/VulkanHelpers.h"

namespace west::rhi
{

class VulkanTexture final : public IRHITexture
{
public:
    VulkanTexture() = default;
    ~VulkanTexture() override = default;

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

private:
    VkImage m_image = VK_NULL_HANDLE; // Non-owning for swapchain
    VkImageView m_imageView = VK_NULL_HANDLE;
    RHITextureDesc m_desc{};
    BindlessIndex m_bindlessIndex = kInvalidBindlessIndex;
};

} // namespace west::rhi
