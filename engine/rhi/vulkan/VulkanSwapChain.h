// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan swap chain — VkSurfaceKHR + VkSwapchainKHR
// =============================================================================
#pragma once

#include "rhi/interface/IRHISwapChain.h"
#include "rhi/vulkan/VulkanHelpers.h"
#include "rhi/vulkan/VulkanTexture.h"

#include <vector>

namespace west::rhi
{

class VulkanDevice;

class VulkanSwapChain final : public IRHISwapChain
{
public:
    VulkanSwapChain() = default;
    ~VulkanSwapChain() override;

    void Initialize(VulkanDevice* device, const RHISwapChainDesc& desc);

    // ── IRHISwapChain interface ───────────────────────────────────────
    uint32_t AcquireNextImage(IRHISemaphore* acquireSemaphore = nullptr) override;
    void Present(IRHISemaphore* presentSemaphore = nullptr) override;
    IRHITexture* GetCurrentBackBuffer() override;
    void Resize(uint32_t width, uint32_t height) override;
    uint32_t GetCurrentIndex() const override
    {
        return m_currentIndex;
    }
    uint32_t GetBufferCount() const override
    {
        return m_bufferCount;
    }
    RHIFormat GetFormat() const override
    {
        return m_format;
    }

private:
    void CreateSurface(void* windowHandle);
    void CreateSwapChain(uint32_t width, uint32_t height);
    void CreateImageViews();
    void DestroySwapChainResources();

    VulkanDevice* m_device = nullptr;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    VkFormat m_vkFormat = VK_FORMAT_B8G8R8A8_UNORM;

    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<VulkanTexture> m_textures;

    uint32_t m_currentIndex = 0;
    uint32_t m_bufferCount = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    RHIFormat m_format = RHIFormat::BGRA8_UNORM;
    bool m_vsync = false;
};

} // namespace west::rhi
