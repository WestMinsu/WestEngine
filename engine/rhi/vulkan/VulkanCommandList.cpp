// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan command list implementation — Phase 1: barrier + ClearColor
// =============================================================================
#include "rhi/vulkan/VulkanCommandList.h"

#include "rhi/vulkan/VulkanTexture.h"

#include <vector>

namespace west::rhi
{

VulkanCommandList::~VulkanCommandList()
{
    if (m_cmdPool && m_device)
    {
        vkDestroyCommandPool(m_device, m_cmdPool, nullptr);
        m_cmdPool = VK_NULL_HANDLE;
        m_cmdBuffer = VK_NULL_HANDLE;
    }
}

void VulkanCommandList::Initialize(VkDevice device, uint32_t queueFamilyIndex, RHIQueueType type)
{
    m_device = device;
    m_queueType = type;

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;

    WEST_VK_CHECK(vkCreateCommandPool(device, &poolInfo, nullptr, &m_cmdPool));

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = m_cmdPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    WEST_VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &m_cmdBuffer));

    WEST_LOG_VERBOSE(LogCategory::RHI, "Vulkan CommandList created.");
}

// ── Recording Lifecycle ───────────────────────────────────────────────────

void VulkanCommandList::Begin()
{
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    WEST_VK_CHECK(vkBeginCommandBuffer(m_cmdBuffer, &beginInfo));
}

void VulkanCommandList::End()
{
    WEST_VK_CHECK(vkEndCommandBuffer(m_cmdBuffer));
}

void VulkanCommandList::Reset()
{
    WEST_VK_CHECK(vkResetCommandBuffer(m_cmdBuffer, 0));
}

// ── Barrier (Synchronization2) ────────────────────────────────────────────

void VulkanCommandList::ResourceBarrier(const RHIBarrierDesc& desc)
{
    if (desc.type == RHIBarrierDesc::Type::Transition && desc.texture)
    {
        auto* vkTex = static_cast<VulkanTexture*>(desc.texture);

        // Convert RHIResourceState to Vulkan layout + access + stage
        auto convertState =
            [](RHIResourceState state) -> std::tuple<VkImageLayout, VkAccessFlags2, VkPipelineStageFlags2>
        {
            if (HasFlag(state, RHIResourceState::RenderTarget))
                return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
            if (HasFlag(state, RHIResourceState::Present))
                return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT};
            if (HasFlag(state, RHIResourceState::ShaderResource))
                return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT};
            if (HasFlag(state, RHIResourceState::CopyDest))
                return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COPY_BIT};
            if (HasFlag(state, RHIResourceState::CopySource))
                return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_TRANSFER_READ_BIT,
                        VK_PIPELINE_STAGE_2_COPY_BIT};
            if (HasFlag(state, RHIResourceState::DepthStencilWrite))
                return {VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT};
            if (HasFlag(state, RHIResourceState::UnorderedAccess))
                return {VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};

            // Undefined / Common
            return {VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
        };

        auto [oldLayout, srcAccess, srcStage] = convertState(desc.stateBefore);
        auto [newLayout, dstAccess, dstStage] = convertState(desc.stateAfter);

        VkImageMemoryBarrier2 imageBarrier{};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imageBarrier.srcStageMask = srcStage;
        imageBarrier.srcAccessMask = srcAccess;
        imageBarrier.dstStageMask = dstStage;
        imageBarrier.dstAccessMask = dstAccess;
        imageBarrier.oldLayout = oldLayout;
        imageBarrier.newLayout = newLayout;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = vkTex->GetVkImage();
        imageBarrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.imageMemoryBarrierCount = 1;
        depInfo.pImageMemoryBarriers = &imageBarrier;

        vkCmdPipelineBarrier2(m_cmdBuffer, &depInfo);
    }
    // TODO(minsu): Buffer barriers, aliasing barriers
}

// ── Viewport & Scissor ────────────────────────────────────────────────────

void VulkanCommandList::SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth)
{
    VkViewport viewport{};
    viewport.x = x;
    viewport.y = y;
    viewport.width = w;
    viewport.height = h;
    viewport.minDepth = minDepth;
    viewport.maxDepth = maxDepth;
    vkCmdSetViewport(m_cmdBuffer, 0, 1, &viewport);
}

void VulkanCommandList::SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    VkRect2D scissor{};
    scissor.offset = {x, y};
    scissor.extent = {w, h};
    vkCmdSetScissor(m_cmdBuffer, 0, 1, &scissor);
}

// ── Render Pass (Dynamic Rendering — Vulkan 1.3) ─────────────────────────

void VulkanCommandList::BeginRenderPass(const RHIRenderPassDesc& desc)
{
    std::vector<VkRenderingAttachmentInfo> colorAttachments;

    for (const auto& attach : desc.colorAttachments)
    {
        if (!attach.texture)
            continue;

        auto* vkTex = static_cast<VulkanTexture*>(attach.texture);

        VkRenderingAttachmentInfo colorInfo{};
        colorInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorInfo.imageView = vkTex->GetVkImageView();
        colorInfo.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        colorInfo.storeOp =
            (attach.storeOp == RHIStoreOp::Store) ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE;

        if (attach.loadOp == RHILoadOp::Clear)
        {
            colorInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorInfo.clearValue.color = {
                {attach.clearColor[0], attach.clearColor[1], attach.clearColor[2], attach.clearColor[3]}};
        }
        else if (attach.loadOp == RHILoadOp::Load)
        {
            colorInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        else
        {
            colorInfo.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }

        colorAttachments.push_back(colorInfo);
    }

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    renderingInfo.pColorAttachments = colorAttachments.data();

    // Determine render area from first attachment
    if (!desc.colorAttachments.empty() && desc.colorAttachments[0].texture)
    {
        auto& texDesc = desc.colorAttachments[0].texture->GetDesc();
        renderingInfo.renderArea.extent = {texDesc.width, texDesc.height};
    }

    vkCmdBeginRendering(m_cmdBuffer, &renderingInfo);
}

void VulkanCommandList::EndRenderPass()
{
    vkCmdEndRendering(m_cmdBuffer);
}

// ── Stub implementations (Phase 2+) ──────────────────────────────────────

void VulkanCommandList::SetPipeline(IRHIPipeline* /*pipeline*/)
{
    // TODO(minsu): Phase 4
}

void VulkanCommandList::SetPushConstants(const void* /*data*/, uint32_t /*sizeBytes*/)
{
    // TODO(minsu): Phase 3
}

void VulkanCommandList::SetVertexBuffer(uint32_t /*slot*/, IRHIBuffer* /*buffer*/, uint64_t /*offset*/)
{
    // TODO(minsu): Phase 2
}

void VulkanCommandList::SetIndexBuffer(IRHIBuffer* /*buffer*/, RHIFormat /*format*/, uint64_t /*offset*/)
{
    // TODO(minsu): Phase 2
}

void VulkanCommandList::Draw(uint32_t /*vertexCount*/, uint32_t /*instanceCount*/, uint32_t /*firstVertex*/,
                             uint32_t /*firstInstance*/)
{
    // TODO(minsu): Phase 4
}

void VulkanCommandList::DrawIndexed(uint32_t /*indexCount*/, uint32_t /*instanceCount*/, uint32_t /*firstIndex*/,
                                    int32_t /*vertexOffset*/, uint32_t /*firstInstance*/)
{
    // TODO(minsu): Phase 4
}

void VulkanCommandList::DrawIndexedIndirectCount(IRHIBuffer* /*argsBuffer*/, uint64_t /*argsOffset*/,
                                                 IRHIBuffer* /*countBuffer*/, uint64_t /*countOffset*/,
                                                 uint32_t /*maxDrawCount*/, uint32_t /*stride*/)
{
    // TODO(minsu): Phase 6
}

void VulkanCommandList::Dispatch(uint32_t /*groupCountX*/, uint32_t /*groupCountY*/, uint32_t /*groupCountZ*/)
{
    // TODO(minsu): Phase 4
}

void VulkanCommandList::CopyBuffer(IRHIBuffer* /*src*/, uint64_t /*srcOffset*/, IRHIBuffer* /*dst*/,
                                   uint64_t /*dstOffset*/, uint64_t /*size*/)
{
    // TODO(minsu): Phase 2
}

void VulkanCommandList::CopyBufferToTexture(IRHIBuffer* /*src*/, IRHITexture* /*dst*/, const RHICopyRegion& /*region*/)
{
    // TODO(minsu): Phase 2
}

void VulkanCommandList::WriteTimestamp(IRHIBuffer* /*queryBuffer*/, uint32_t /*index*/)
{
    // TODO(minsu): Phase 7
}

} // namespace west::rhi
