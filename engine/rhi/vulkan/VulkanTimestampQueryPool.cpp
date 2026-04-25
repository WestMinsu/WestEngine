// =============================================================================
// WestEngine - RHI Vulkan
// Timestamp query pool implementation
// =============================================================================
#include "rhi/vulkan/VulkanTimestampQueryPool.h"

#include "rhi/vulkan/VulkanDevice.h"

namespace west::rhi
{

VulkanTimestampQueryPool::~VulkanTimestampQueryPool()
{
    if (m_queryPool != VK_NULL_HANDLE && m_device != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(m_device, m_queryPool, nullptr);
        m_queryPool = VK_NULL_HANDLE;
    }
}

void VulkanTimestampQueryPool::Initialize(VulkanDevice* device, const RHITimestampQueryPoolDesc& desc)
{
    WEST_ASSERT(device != nullptr);
    WEST_ASSERT(desc.queryCount > 0);

    m_device = device->GetVkDevice();
    m_desc = desc;

    VkQueryPoolCreateInfo queryPoolInfo{};
    queryPoolInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    queryPoolInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
    queryPoolInfo.queryCount = desc.queryCount;
    WEST_VK_CHECK(vkCreateQueryPool(m_device, &queryPoolInfo, nullptr, &m_queryPool));

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(device->GetVkPhysicalDevice(), &properties);
    m_timestampPeriodNanoseconds = properties.limits.timestampPeriod;
}

bool VulkanTimestampQueryPool::ReadTimestamps(uint32_t firstQuery, uint32_t queryCount,
                                              std::span<uint64_t> timestamps)
{
    if (queryCount == 0)
    {
        return true;
    }
    if (firstQuery + queryCount > m_desc.queryCount || timestamps.size() < queryCount)
    {
        return false;
    }

    const VkResult result = vkGetQueryPoolResults(
        m_device,
        m_queryPool,
        firstQuery,
        queryCount,
        queryCount * sizeof(uint64_t),
        timestamps.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT);

    if (result == VK_NOT_READY)
    {
        return false;
    }

    WEST_VK_CHECK(result);
    return true;
}

} // namespace west::rhi
