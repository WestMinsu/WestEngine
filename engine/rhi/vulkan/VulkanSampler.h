// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan sampler wrapper
// =============================================================================
#pragma once

#include "rhi/interface/IRHISampler.h"
#include "rhi/vulkan/VulkanHelpers.h"

namespace west::rhi
{

class VulkanDevice;

class VulkanSampler final : public IRHISampler
{
public:
    VulkanSampler() = default;
    ~VulkanSampler() override;

    void Initialize(VulkanDevice* device, const RHISamplerDesc& desc);

    const RHISamplerDesc& GetDesc() const override
    {
        return m_desc;
    }

    BindlessIndex GetBindlessIndex() const override
    {
        return m_bindlessIndex;
    }

    VkSampler GetVkSampler() const
    {
        return m_sampler;
    }

    void SetBindlessIndex(BindlessIndex index)
    {
        m_bindlessIndex = index;
    }

private:
    VkSampler m_sampler = VK_NULL_HANDLE;
    RHISamplerDesc m_desc{};
    BindlessIndex m_bindlessIndex = kInvalidBindlessIndex;
    VulkanDevice* m_device = nullptr;
};

} // namespace west::rhi
