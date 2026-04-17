// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan fence implementation — VkTimelineSemaphore
// =============================================================================
#include "rhi/vulkan/VulkanFence.h"

namespace west::rhi
{

VulkanFence::~VulkanFence()
{
    if (m_timelineSemaphore && m_device)
    {
        vkDestroySemaphore(m_device, m_timelineSemaphore, nullptr);
        m_timelineSemaphore = VK_NULL_HANDLE;
    }
}

void VulkanFence::Initialize(VkDevice device, uint64_t initialValue)
{
    m_device = device;
    m_nextValue.store(initialValue, std::memory_order_relaxed);

    VkSemaphoreTypeCreateInfo timelineInfo{};
    timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineInfo.initialValue = initialValue;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semaphoreInfo.pNext = &timelineInfo;

    WEST_VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_timelineSemaphore));

    WEST_LOG_VERBOSE(LogCategory::RHI, "Vulkan Timeline Semaphore created (initial value: {})", initialValue);
}

uint64_t VulkanFence::GetCompletedValue() const
{
    uint64_t value = 0;
    WEST_VK_CHECK(vkGetSemaphoreCounterValue(m_device, m_timelineSemaphore, &value));
    return value;
}

void VulkanFence::Wait(uint64_t value, uint64_t timeoutNs)
{
    // Early out if already completed
    uint64_t currentValue = 0;
    vkGetSemaphoreCounterValue(m_device, m_timelineSemaphore, &currentValue);
    if (currentValue >= value)
    {
        return;
    }

    VkSemaphoreWaitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_timelineSemaphore;
    waitInfo.pValues = &value;

    VkResult result = vkWaitSemaphores(m_device, &waitInfo, timeoutNs);
    if (result == VK_TIMEOUT)
    {
        WEST_LOG_WARNING(LogCategory::RHI, "Vulkan Timeline Semaphore wait timed out (target: {}, completed: {})",
                         value, GetCompletedValue());
    }
    else
    {
        WEST_VK_CHECK(result);
    }
}

uint64_t VulkanFence::AdvanceValue()
{
    return m_nextValue.fetch_add(1, std::memory_order_relaxed) + 1;
}

} // namespace west::rhi
