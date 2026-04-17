// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan queue implementation — Synchronization2 submit
// =============================================================================
#include "rhi/vulkan/VulkanQueue.h"

#include "rhi/vulkan/VulkanCommandList.h"
#include "rhi/vulkan/VulkanFence.h"
#include "rhi/vulkan/VulkanSemaphore.h"

namespace west::rhi
{

void VulkanQueue::Initialize(VkQueue queue, uint32_t familyIndex, RHIQueueType type)
{
    m_queue = queue;
    m_familyIndex = familyIndex;
    m_type = type;

    WEST_LOG_INFO(LogCategory::RHI, "Vulkan {} Queue acquired (family: {}).",
                  (type == RHIQueueType::Graphics)  ? "Graphics"
                  : (type == RHIQueueType::Compute) ? "Compute"
                                                    : "Copy",
                  familyIndex);
}

void VulkanQueue::Submit(const RHISubmitInfo& info)
{
    WEST_ASSERT(info.commandList != nullptr);

    auto* vkCmdList = static_cast<VulkanCommandList*>(info.commandList);

    VkCommandBufferSubmitInfo cmdBufInfo{};
    cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdBufInfo.commandBuffer = vkCmdList->GetVkCommandBuffer();

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &cmdBufInfo;

    // Wait semaphore (binary — swapchain acquire)
    VkSemaphoreSubmitInfo waitSemInfo{};
    if (info.waitSemaphore)
    {
        auto* vkWaitSem = static_cast<VulkanSemaphore*>(info.waitSemaphore);
        waitSemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitSemInfo.semaphore = vkWaitSem->GetVkSemaphore();
        waitSemInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        submitInfo.waitSemaphoreInfoCount = 1;
        submitInfo.pWaitSemaphoreInfos = &waitSemInfo;
    }

    // Signal semaphores — timeline fence + binary present
    VkSemaphoreSubmitInfo signalInfos[2]{};
    uint32_t signalCount = 0;

    // Timeline semaphore (fence)
    if (info.signalFence)
    {
        auto* vkFence = static_cast<VulkanFence*>(info.signalFence);
        signalInfos[signalCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfos[signalCount].semaphore = vkFence->GetVkSemaphore();
        signalInfos[signalCount].value = info.signalValue;
        signalInfos[signalCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalCount++;
    }

    // Binary semaphore (present)
    if (info.signalSemaphore)
    {
        auto* vkSigSem = static_cast<VulkanSemaphore*>(info.signalSemaphore);
        signalInfos[signalCount].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfos[signalCount].semaphore = vkSigSem->GetVkSemaphore();
        signalInfos[signalCount].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalCount++;
    }

    submitInfo.signalSemaphoreInfoCount = signalCount;
    submitInfo.pSignalSemaphoreInfos = signalInfos;

    WEST_VK_CHECK(vkQueueSubmit2(m_queue, 1, &submitInfo, VK_NULL_HANDLE));
}

} // namespace west::rhi
