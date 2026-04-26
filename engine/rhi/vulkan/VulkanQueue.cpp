// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan queue implementation — Synchronization2 submit
// =============================================================================
#include "rhi/vulkan/VulkanQueue.h"

#include "rhi/vulkan/VulkanCommandList.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanFence.h"
#include "rhi/vulkan/VulkanSemaphore.h"

#include <vector>

namespace west::rhi
{

namespace
{

[[nodiscard]] VkPipelineStageFlags2 ConvertPipelineStageMask(RHIPipelineStage stageMask)
{
    if (stageMask == RHIPipelineStage::Auto)
    {
        return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
    if (HasFlag(stageMask, RHIPipelineStage::AllCommands))
    {
        return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }

    VkPipelineStageFlags2 result = 0;
    if (HasFlag(stageMask, RHIPipelineStage::TopOfPipe))
    {
        result |= VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
    }
    if (HasFlag(stageMask, RHIPipelineStage::DrawIndirect))
    {
        result |= VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;
    }
    if (HasFlag(stageMask, RHIPipelineStage::VertexInput))
    {
        result |= VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT;
    }
    if (HasFlag(stageMask, RHIPipelineStage::VertexShader))
    {
        result |= VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    }
    if (HasFlag(stageMask, RHIPipelineStage::PixelShader))
    {
        result |= VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    }
    if (HasFlag(stageMask, RHIPipelineStage::ComputeShader))
    {
        result |= VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    }
    if (HasFlag(stageMask, RHIPipelineStage::ColorAttachmentOutput))
    {
        result |= VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    }
    if (HasFlag(stageMask, RHIPipelineStage::DepthStencil))
    {
        result |= VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
    }
    if (HasFlag(stageMask, RHIPipelineStage::Copy))
    {
        result |= VK_PIPELINE_STAGE_2_COPY_BIT;
    }
    if (HasFlag(stageMask, RHIPipelineStage::BottomOfPipe))
    {
        result |= VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
    }
    if (HasFlag(stageMask, RHIPipelineStage::AllGraphics))
    {
        result |= VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT;
    }

    return result != 0 ? result : VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
}

} // namespace

void VulkanQueue::Initialize(VulkanDevice* ownerDevice, VkQueue queue, uint32_t familyIndex, uint32_t queueIndex,
                             RHIQueueType type)
{
    m_ownerDevice = ownerDevice;
    m_queue = queue;
    m_familyIndex = familyIndex;
    m_queueIndex = queueIndex;
    m_type = type;

    WEST_LOG_INFO(LogCategory::RHI, "Vulkan {} Queue acquired (family: {}, index: {}).",
                  (type == RHIQueueType::Graphics)  ? "Graphics"
                  : (type == RHIQueueType::Compute) ? "Compute"
                                                    : "Copy",
                  familyIndex, queueIndex);
}

void VulkanQueue::Submit(const RHISubmitInfo& info)
{
    WEST_CHECK(!info.commandLists.empty(), "VulkanQueue::Submit requires at least one command list");

    if (m_ownerDevice)
    {
        m_ownerDevice->FlushPendingBindlessDescriptorWrites();
    }

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
        waitInfo.stageMask = ConvertPipelineStageMask(waitDesc.stageMask);
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

    std::lock_guard<std::mutex> queueLock(m_queueMutex);
    WEST_VK_CHECK(vkQueueSubmit2(m_queue, 1, &submitInfo, VK_NULL_HANDLE));
}

VkResult VulkanQueue::Present(const VkPresentInfoKHR& presentInfo)
{
    std::lock_guard<std::mutex> queueLock(m_queueMutex);
    return vkQueuePresentKHR(m_queue, &presentInfo);
}

} // namespace west::rhi
