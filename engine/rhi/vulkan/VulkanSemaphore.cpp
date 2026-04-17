// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan binary semaphore implementation
// =============================================================================
#include "rhi/vulkan/VulkanSemaphore.h"

namespace west::rhi
{

VulkanSemaphore::~VulkanSemaphore()
{
    if (m_semaphore && m_device)
    {
        vkDestroySemaphore(m_device, m_semaphore, nullptr);
        m_semaphore = VK_NULL_HANDLE;
    }
}

void VulkanSemaphore::Initialize(VkDevice device)
{
    m_device = device;

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    WEST_VK_CHECK(vkCreateSemaphore(device, &semaphoreInfo, nullptr, &m_semaphore));
}

} // namespace west::rhi
