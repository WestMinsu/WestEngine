// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan binary semaphore — Swapchain Acquire/Present synchronization
// =============================================================================
#pragma once

#include "rhi/interface/IRHISemaphore.h"
#include "rhi/vulkan/VulkanHelpers.h"

namespace west::rhi
{

class VulkanSemaphore final : public IRHISemaphore
{
public:
    VulkanSemaphore() = default;
    ~VulkanSemaphore() override;
    VulkanSemaphore(const VulkanSemaphore&) = delete;
    VulkanSemaphore& operator=(const VulkanSemaphore&) = delete;
    VulkanSemaphore(VulkanSemaphore&&) = delete;
    VulkanSemaphore& operator=(VulkanSemaphore&&) = delete;

    void Initialize(VkDevice device);

    // ── Internal ──────────────────────────────────────────────────────
    VkSemaphore GetVkSemaphore() const
    {
        return m_semaphore;
    }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkSemaphore m_semaphore = VK_NULL_HANDLE;
};

} // namespace west::rhi
