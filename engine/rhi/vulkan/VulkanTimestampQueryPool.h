// =============================================================================
// WestEngine - RHI Vulkan
// Timestamp query pool
// =============================================================================
#pragma once

#include "rhi/interface/IRHITimestampQueryPool.h"
#include "rhi/vulkan/VulkanHelpers.h"

namespace west::rhi
{

class VulkanDevice;

class VulkanTimestampQueryPool final : public IRHITimestampQueryPool
{
public:
    VulkanTimestampQueryPool() = default;
    ~VulkanTimestampQueryPool() override;
    VulkanTimestampQueryPool(const VulkanTimestampQueryPool&) = delete;
    VulkanTimestampQueryPool& operator=(const VulkanTimestampQueryPool&) = delete;
    VulkanTimestampQueryPool(VulkanTimestampQueryPool&&) = delete;
    VulkanTimestampQueryPool& operator=(VulkanTimestampQueryPool&&) = delete;

    void Initialize(VulkanDevice* device, const RHITimestampQueryPoolDesc& desc);

    const RHITimestampQueryPoolDesc& GetDesc() const override
    {
        return m_desc;
    }

    float GetTimestampPeriodNanoseconds() const override
    {
        return m_timestampPeriodNanoseconds;
    }

    bool ReadTimestamps(uint32_t firstQuery, uint32_t queryCount,
                        std::span<uint64_t> timestamps) override;

    VkQueryPool GetVkQueryPool() const
    {
        return m_queryPool;
    }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueryPool m_queryPool = VK_NULL_HANDLE;
    RHITimestampQueryPoolDesc m_desc{};
    float m_timestampPeriodNanoseconds = 0.0f;
};

} // namespace west::rhi
