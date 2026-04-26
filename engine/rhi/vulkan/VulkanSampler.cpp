// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan sampler implementation
// =============================================================================
#include "rhi/vulkan/VulkanSampler.h"

#include "rhi/vulkan/VulkanDevice.h"

#include <algorithm>

namespace west::rhi
{

static VkFilter ToVkFilter(RHIFilter filter)
{
    return filter == RHIFilter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

static VkSamplerMipmapMode ToVkMipmapMode(RHIMipmapMode mode)
{
    return mode == RHIMipmapMode::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
}

static VkSamplerAddressMode ToVkAddressMode(RHIAddressMode mode)
{
    switch (mode)
    {
    case RHIAddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case RHIAddressMode::MirroredRepeat:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case RHIAddressMode::ClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case RHIAddressMode::ClampToEdge:
    default:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
}

static VkCompareOp ToVkCompareOp(RHICompareOp op)
{
    switch (op)
    {
    case RHICompareOp::Never:
        return VK_COMPARE_OP_NEVER;
    case RHICompareOp::Less:
        return VK_COMPARE_OP_LESS;
    case RHICompareOp::Equal:
        return VK_COMPARE_OP_EQUAL;
    case RHICompareOp::LessEqual:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case RHICompareOp::Greater:
        return VK_COMPARE_OP_GREATER;
    case RHICompareOp::NotEqual:
        return VK_COMPARE_OP_NOT_EQUAL;
    case RHICompareOp::GreaterEqual:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case RHICompareOp::Always:
    default:
        return VK_COMPARE_OP_ALWAYS;
    }
}

static VkBorderColor ToVkBorderColor(RHIBorderColor color)
{
    switch (color)
    {
    case RHIBorderColor::TransparentBlack:
        return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    case RHIBorderColor::OpaqueWhite:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    case RHIBorderColor::OpaqueBlack:
    default:
        return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    }
}

VulkanSampler::~VulkanSampler()
{
    if (m_device && m_bindlessIndex != kInvalidBindlessIndex)
    {
        m_device->UnregisterBindlessResource(this);
    }

    if (m_sampler && m_device)
    {
        VkDevice device = m_device->GetVkDevice();
        VkSampler sampler = m_sampler;
        m_device->EnqueueDeferredDeletion(
            [device, sampler]() {
                vkDestroySampler(device, sampler, nullptr);
            },
            m_device->GetCurrentFrameFenceValue());
    }

    m_sampler = VK_NULL_HANDLE;
}

void VulkanSampler::Initialize(VulkanDevice* device, const RHISamplerDesc& desc)
{
    WEST_ASSERT(device != nullptr);
    m_device = device;
    m_desc = desc;

    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = ToVkFilter(desc.magFilter);
    samplerInfo.minFilter = ToVkFilter(desc.minFilter);
    samplerInfo.mipmapMode = ToVkMipmapMode(desc.mipmapMode);
    samplerInfo.addressModeU = ToVkAddressMode(desc.addressU);
    samplerInfo.addressModeV = ToVkAddressMode(desc.addressV);
    samplerInfo.addressModeW = ToVkAddressMode(desc.addressW);
    samplerInfo.mipLodBias = desc.mipLodBias;
    samplerInfo.anisotropyEnable = desc.anisotropyEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.maxAnisotropy = desc.anisotropyEnable ?
        std::clamp(desc.maxAnisotropy, 1.0f, device->GetMaxSamplerAnisotropy()) :
        1.0f;
    samplerInfo.compareEnable = desc.compareEnable ? VK_TRUE : VK_FALSE;
    samplerInfo.compareOp = ToVkCompareOp(desc.compareOp);
    samplerInfo.minLod = desc.minLod;
    samplerInfo.maxLod = desc.maxLod;
    samplerInfo.borderColor = ToVkBorderColor(desc.borderColor);
    samplerInfo.unnormalizedCoordinates = VK_FALSE;

    WEST_VK_CHECK(vkCreateSampler(device->GetVkDevice(), &samplerInfo, nullptr, &m_sampler));
}

} // namespace west::rhi
