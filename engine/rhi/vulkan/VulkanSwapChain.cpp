// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan swap chain implementation — Win32 Surface + VkSwapchainKHR
// =============================================================================
#include "rhi/vulkan/VulkanSwapChain.h"

#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanQueue.h"
#include "rhi/vulkan/VulkanSemaphore.h"

// WSI requires platform headers — this is the only Vulkan file that includes Win32
#include "platform/win32/Win32Headers.h"

#include <algorithm>
#include <vulkan/vulkan_win32.h>

namespace west::rhi
{

VulkanSwapChain::~VulkanSwapChain()
{
    if (m_device)
    {
        vkDeviceWaitIdle(m_device->GetVkDevice());
    }

    DestroySwapChainResources();

    if (m_swapChain && m_device)
    {
        vkDestroySwapchainKHR(m_device->GetVkDevice(), m_swapChain, nullptr);
        m_swapChain = VK_NULL_HANDLE;
    }

    if (m_surface && m_device)
    {
        vkDestroySurfaceKHR(m_device->GetVkInstance(), m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }

    WEST_LOG_INFO(LogCategory::RHI, "Vulkan SwapChain destroyed.");
}

void VulkanSwapChain::Initialize(VulkanDevice* device, const RHISwapChainDesc& desc)
{
    m_device = device;
    m_vsync = desc.vsync;
    m_format = desc.format;
    m_width = desc.width;
    m_height = desc.height;

    CreateSurface(desc.windowHandle);

    // Verify present support
    VkBool32 presentSupport = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device->GetVkPhysicalDevice(), device->GetGraphicsQueueFamily(), m_surface,
                                         &presentSupport);
    WEST_CHECK(presentSupport == VK_TRUE, "Graphics queue does not support presentation");

    CreateSwapChain(desc.width, desc.height);
    CreateImageViews();

    WEST_LOG_INFO(LogCategory::RHI, "Vulkan SwapChain created: {}x{}, {} images", desc.width, desc.height,
                  m_bufferCount);
}

void VulkanSwapChain::CreateSurface(void* windowHandle)
{
    VkWin32SurfaceCreateInfoKHR surfaceInfo{};
    surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceInfo.hinstance = ::GetModuleHandleW(nullptr);
    surfaceInfo.hwnd = static_cast<HWND>(windowHandle);

    WEST_VK_CHECK(vkCreateWin32SurfaceKHR(m_device->GetVkInstance(), &surfaceInfo, nullptr, &m_surface));
}

void VulkanSwapChain::CreateSwapChain(uint32_t width, uint32_t height)
{
    VkPhysicalDevice physDevice = m_device->GetVkPhysicalDevice();

    // Query surface capabilities
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice, m_surface, &capabilities);

    // Query supported formats
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, m_surface, &formatCount, formats.data());

    // Prefer BGRA8 SRGB, fallback to first available
    m_vkFormat = formats[0].format;
    VkColorSpaceKHR colorSpace = formats[0].colorSpace;
    for (const auto& fmt : formats)
    {
        if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            m_vkFormat = fmt.format;
            colorSpace = fmt.colorSpace;
            m_format = RHIFormat::BGRA8_UNORM;
            break;
        }
    }

    // Determine image count
    m_bufferCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && m_bufferCount > capabilities.maxImageCount)
    {
        m_bufferCount = capabilities.maxImageCount;
    }

    // Determine extent
    VkExtent2D extent;
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        extent = capabilities.currentExtent;
    }
    else
    {
        extent.width = std::clamp(width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    m_width = extent.width;
    m_height = extent.height;

    // Query present modes
    uint32_t presentModeCount;
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(physDevice, m_surface, &presentModeCount, presentModes.data());

    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR; // Guaranteed available (vsync)
    if (!m_vsync)
    {
        for (const auto& mode : presentModes)
        {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
            if (mode == VK_PRESENT_MODE_IMMEDIATE_KHR)
            {
                presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
            }
        }
    }

    VkSwapchainCreateInfoKHR swapChainInfo{};
    swapChainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapChainInfo.surface = m_surface;
    swapChainInfo.minImageCount = m_bufferCount;
    swapChainInfo.imageFormat = m_vkFormat;
    swapChainInfo.imageColorSpace = colorSpace;
    swapChainInfo.imageExtent = extent;
    swapChainInfo.imageArrayLayers = 1;
    swapChainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapChainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapChainInfo.preTransform = capabilities.currentTransform;
    swapChainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapChainInfo.presentMode = presentMode;
    swapChainInfo.clipped = VK_TRUE;
    swapChainInfo.oldSwapchain = VK_NULL_HANDLE;

    WEST_VK_CHECK(vkCreateSwapchainKHR(m_device->GetVkDevice(), &swapChainInfo, nullptr, &m_swapChain));

    // Get actual image count (may differ from requested)
    vkGetSwapchainImagesKHR(m_device->GetVkDevice(), m_swapChain, &m_bufferCount, nullptr);
    m_images.resize(m_bufferCount);
    vkGetSwapchainImagesKHR(m_device->GetVkDevice(), m_swapChain, &m_bufferCount, m_images.data());
}

void VulkanSwapChain::CreateImageViews()
{
    m_imageViews.resize(m_bufferCount);
    m_textures.resize(m_bufferCount);

    for (uint32_t i = 0; i < m_bufferCount; ++i)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = m_vkFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        WEST_VK_CHECK(vkCreateImageView(m_device->GetVkDevice(), &viewInfo, nullptr, &m_imageViews[i]));

        RHITextureDesc texDesc{};
        texDesc.width = m_width;
        texDesc.height = m_height;
        texDesc.format = m_format;
        texDesc.usage = RHITextureUsage::RenderTarget | RHITextureUsage::Present;

        m_textures[i].InitFromSwapChain(m_images[i], m_imageViews[i], texDesc);
    }
}

void VulkanSwapChain::DestroySwapChainResources()
{
    m_textures.clear();

    if (m_device)
    {
        for (auto& view : m_imageViews)
        {
            if (view)
            {
                vkDestroyImageView(m_device->GetVkDevice(), view, nullptr);
            }
        }
    }
    m_imageViews.clear();
    m_images.clear();
}

// ── IRHISwapChain interface ───────────────────────────────────────────────

uint32_t VulkanSwapChain::AcquireNextImage(IRHISemaphore* acquireSemaphore)
{
    VkSemaphore vkSemaphore = VK_NULL_HANDLE;
    if (acquireSemaphore)
    {
        vkSemaphore = static_cast<VulkanSemaphore*>(acquireSemaphore)->GetVkSemaphore();
    }

    VkResult result = vkAcquireNextImageKHR(m_device->GetVkDevice(), m_swapChain, UINT64_MAX, vkSemaphore,
                                            VK_NULL_HANDLE, &m_currentIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        WEST_LOG_WARNING(LogCategory::RHI, "Swapchain out of date — resize needed");
        return UINT32_MAX;
    }
    else if (result != VK_SUBOPTIMAL_KHR)
    {
        WEST_VK_CHECK(result);
    }

    return m_currentIndex;
}

bool VulkanSwapChain::Present(IRHISemaphore* presentSemaphore)
{
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain;
    presentInfo.pImageIndices = &m_currentIndex;

    if (presentSemaphore)
    {
        VkSemaphore vkSem = static_cast<VulkanSemaphore*>(presentSemaphore)->GetVkSemaphore();
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &vkSem;
    }

    auto* vkQueue = static_cast<VulkanQueue*>(m_device->GetQueue(RHIQueueType::Graphics));

    VkResult result = vkQueuePresentKHR(vkQueue->GetVkQueue(), &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        WEST_LOG_WARNING(LogCategory::RHI, "Swapchain needs resize after present");
        return false;
    }
    else
    {
        WEST_VK_CHECK(result);
    }

    return true;
}

IRHITexture* VulkanSwapChain::GetCurrentBackBuffer()
{
    return &m_textures[m_currentIndex];
}

void VulkanSwapChain::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
        return;

    vkDeviceWaitIdle(m_device->GetVkDevice());

    DestroySwapChainResources();

    VkSwapchainKHR oldSwapChain = m_swapChain;
    m_swapChain = VK_NULL_HANDLE;

    CreateSwapChain(width, height);
    CreateImageViews();

    if (oldSwapChain)
    {
        vkDestroySwapchainKHR(m_device->GetVkDevice(), oldSwapChain, nullptr);
    }

    WEST_LOG_INFO(LogCategory::RHI, "Vulkan SwapChain resized: {}x{}", width, height);
}

// ── VulkanTexture helper ──────────────────────────────────────────────────

void VulkanTexture::InitFromSwapChain(VkImage image, VkImageView imageView, const RHITextureDesc& desc)
{
    m_image = image;
    m_imageView = imageView;
    m_desc = desc;
}

} // namespace west::rhi
