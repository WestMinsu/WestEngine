// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan fence — VkTimelineSemaphore wrapper for CPU-GPU synchronization
// =============================================================================
#pragma once

#include "rhi/interface/IRHIFence.h"
#include "rhi/vulkan/VulkanHelpers.h"

#include <atomic>

namespace west::rhi
{

class VulkanFence final : public IRHIFence
{
public:
    VulkanFence() = default;
    ~VulkanFence() override;

    void Initialize(VkDevice device, uint64_t initialValue = 0);

    // ── IRHIFence interface ───────────────────────────────────────────
    uint64_t GetCompletedValue() const override;
    void Wait(uint64_t value, uint64_t timeoutNs = UINT64_MAX) override;
    uint64_t AdvanceValue() override;

    // ── Internal ──────────────────────────────────────────────────────
    VkSemaphore GetVkSemaphore() const
    {
        return m_timelineSemaphore;
    }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkSemaphore m_timelineSemaphore = VK_NULL_HANDLE;
    std::atomic<uint64_t> m_nextValue{0};
};

} // namespace west::rhi
