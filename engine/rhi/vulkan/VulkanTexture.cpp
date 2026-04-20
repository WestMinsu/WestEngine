// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan texture implementation
// =============================================================================
#include "rhi/vulkan/VulkanTexture.h"

#include "rhi/common/FormatConversion.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanMemoryAllocator.h"

namespace west::rhi
{

VulkanTexture::~VulkanTexture()
{
    if (m_ownsImage && m_image && m_vmaAllocator)
    {
        VkDevice device = m_device ? m_device->GetVkDevice() : VK_NULL_HANDLE;
        VkImageView imageView = m_imageView;
        VkImage image = m_image;
        VmaAllocation allocation = m_allocation;
        VmaAllocator allocator = m_vmaAllocator;

        if (m_device)
        {
            m_device->EnqueueDeferredDeletion(
                [device, imageView, allocator, image, allocation]() {
                    if (imageView)
                    {
                        vkDestroyImageView(device, imageView, nullptr);
                    }
                    vmaDestroyImage(allocator, image, allocation);
                },
                m_device->GetCurrentFrameFenceValue());
        }
        else
        {
            if (imageView)
            {
                vkDestroyImageView(device, imageView, nullptr);
            }
            vmaDestroyImage(allocator, image, allocation);
        }
    }

    m_image = VK_NULL_HANDLE;
    m_imageView = VK_NULL_HANDLE;
    m_allocation = VK_NULL_HANDLE;
}

void VulkanTexture::Initialize(VulkanDevice* device, const RHITextureDesc& desc)
{
    WEST_ASSERT(device != nullptr);
    WEST_ASSERT(desc.width > 0 && desc.height > 0 && desc.depth > 0);
    WEST_ASSERT(desc.dimension == RHITextureDim::Tex2D);

    VulkanMemoryAllocator* allocator = device->GetAllocator();
    WEST_ASSERT(allocator != nullptr);

    m_device = device;
    m_vmaAllocator = allocator->GetAllocator();
    m_desc = desc;
    m_ownsImage = true;

    VkImageUsageFlags usageFlags = 0;
    if (HasFlag(desc.usage, RHITextureUsage::ShaderResource))
        usageFlags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::UnorderedAccess))
        usageFlags |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::RenderTarget))
        usageFlags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil))
        usageFlags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::CopySource))
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (HasFlag(desc.usage, RHITextureUsage::CopyDest))
        usageFlags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = static_cast<VkFormat>(ToVkFormat(desc.format));
    imageInfo.extent = {desc.width, desc.height, desc.depth};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = desc.arrayLayers;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usageFlags;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

    WEST_VK_CHECK(vmaCreateImage(m_vmaAllocator, &imageInfo, &allocInfo, &m_image, &m_allocation, nullptr));

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = m_image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = static_cast<VkFormat>(ToVkFormat(desc.format));
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = desc.arrayLayers;

    WEST_VK_CHECK(vkCreateImageView(device->GetVkDevice(), &viewInfo, nullptr, &m_imageView));

    WEST_LOG_VERBOSE(LogCategory::RHI, "Vulkan Texture created: {} ({}x{})",
                     desc.debugName ? desc.debugName : "unnamed", desc.width, desc.height);
}

void VulkanTexture::InitFromSwapChain(VkImage image, VkImageView imageView, const RHITextureDesc& desc)
{
    m_image = image;
    m_imageView = imageView;
    m_desc = desc;
    m_ownsImage = false;
}

} // namespace west::rhi
