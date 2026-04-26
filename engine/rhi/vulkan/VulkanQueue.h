// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan queue — vkQueueSubmit2 (Synchronization2)
// =============================================================================
#pragma once

#include "rhi/interface/IRHIQueue.h"
#include "rhi/vulkan/VulkanHelpers.h"

#include <mutex>

namespace west::rhi
{

class VulkanDevice;

class VulkanQueue final : public IRHIQueue
{
public:
    VulkanQueue() = default;
    ~VulkanQueue() override = default;

    void Initialize(VulkanDevice* ownerDevice, VkQueue queue, uint32_t familyIndex, uint32_t queueIndex,
                    RHIQueueType type);

    // ── IRHIQueue interface ───────────────────────────────────────────
    void Submit(const RHISubmitInfo& info) override;
    RHIQueueType GetType() const override
    {
        return m_type;
    }

    // ── Internal ──────────────────────────────────────────────────────
    VkQueue GetVkQueue() const
    {
        return m_queue;
    }
    uint32_t GetFamilyIndex() const
    {
        return m_familyIndex;
    }
    uint32_t GetQueueIndex() const
    {
        return m_queueIndex;
    }
    VkResult Present(const VkPresentInfoKHR& presentInfo);

private:
    VkQueue m_queue = VK_NULL_HANDLE;
    uint32_t m_familyIndex = 0;
    uint32_t m_queueIndex = 0;
    RHIQueueType m_type = RHIQueueType::Graphics;
    VulkanDevice* m_ownerDevice = nullptr;
    std::mutex m_queueMutex;
};

} // namespace west::rhi
