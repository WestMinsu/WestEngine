// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan queue implementation — Synchronization2 submit
// =============================================================================
#include "rhi/vulkan/VulkanQueue.h"

#include "rhi/vulkan/VulkanCommandList.h"
#include "rhi/vulkan/VulkanFence.h"
#include "rhi/vulkan/VulkanSemaphore.h"

#include <vector>

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
    WEST_ASSERT(!info.commandLists.empty());

    std::vector<VkCommandBufferSubmitInfo> commandBufferInfos;
    commandBufferInfos.reserve(info.commandLists.size());
    for (IRHICommandList* commandList : info.commandLists)
    {
        auto* vkCmdList = static_cast<VulkanCommandList*>(commandList);
        VkCommandBufferSubmitInfo cmdBufInfo{};
        cmdBufInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        cmdBufInfo.commandBuffer = vkCmdList->GetVkCommandBuffer();
        commandBufferInfos.push_back(cmdBufInfo);
    }

    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.commandBufferInfoCount = static_cast<uint32_t>(commandBufferInfos.size());
    submitInfo.pCommandBufferInfos = commandBufferInfos.data();

    std::vector<VkSemaphoreSubmitInfo> waitInfos;
    waitInfos.reserve(info.timelineWaits.size() + (info.waitSemaphore ? 1u : 0u));
    if (info.waitSemaphore)
    {
        auto* vkWaitSem = static_cast<VulkanSemaphore*>(info.waitSemaphore);
        VkSemaphoreSubmitInfo waitSemInfo{};
        waitSemInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitSemInfo.semaphore = vkWaitSem->GetVkSemaphore();
        waitSemInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitInfos.push_back(waitSemInfo);
    }

    for (const RHITimelineWaitDesc& waitDesc : info.timelineWaits)
    {
        if (!waitDesc.fence)
        {
            continue;
        }

        auto* vkFence = static_cast<VulkanFence*>(waitDesc.fence);
        VkSemaphoreSubmitInfo waitInfo{};
        waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        waitInfo.semaphore = vkFence->GetVkSemaphore();
        waitInfo.value = waitDesc.value;
        waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        waitInfos.push_back(waitInfo);
    }

    submitInfo.waitSemaphoreInfoCount = static_cast<uint32_t>(waitInfos.size());
    submitInfo.pWaitSemaphoreInfos = waitInfos.data();

    std::vector<VkSemaphoreSubmitInfo> signalInfos;
    signalInfos.reserve(info.timelineSignals.size() + (info.signalSemaphore ? 1u : 0u));
    for (const RHITimelineSignalDesc& signalDesc : info.timelineSignals)
    {
        if (!signalDesc.fence)
        {
            continue;
        }

        auto* vkFence = static_cast<VulkanFence*>(signalDesc.fence);
        VkSemaphoreSubmitInfo signalInfo{};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfo.semaphore = vkFence->GetVkSemaphore();
        signalInfo.value = signalDesc.value;
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalInfos.push_back(signalInfo);
    }

    if (info.signalSemaphore)
    {
        auto* vkSigSem = static_cast<VulkanSemaphore*>(info.signalSemaphore);
        VkSemaphoreSubmitInfo signalInfo{};
        signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
        signalInfo.semaphore = vkSigSem->GetVkSemaphore();
        signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
        signalInfos.push_back(signalInfo);
    }

    submitInfo.signalSemaphoreInfoCount = static_cast<uint32_t>(signalInfos.size());
    submitInfo.pSignalSemaphoreInfos = signalInfos.data();

    WEST_VK_CHECK(vkQueueSubmit2(m_queue, 1, &submitInfo, VK_NULL_HANDLE));
}

} // namespace west::rhi
