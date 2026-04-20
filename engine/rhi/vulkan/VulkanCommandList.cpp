// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan command list implementation — Phase 1: barrier + ClearColor
// =============================================================================
#include "rhi/vulkan/VulkanCommandList.h"

#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanPipeline.h"
#include "rhi/vulkan/VulkanTexture.h"
#include "rhi/common/FormatConversion.h"

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

void VulkanCommandList::Initialize(VkDevice device, uint32_t queueFamilyIndex, RHIQueueType type,
                                   VkDeviceAddress bindlessDescriptorBufferAddress,
                                   PFN_vkCmdBindDescriptorBuffersEXT bindDescriptorBuffers,
                                   PFN_vkCmdSetDescriptorBufferOffsetsEXT setDescriptorBufferOffsets)
{
    m_device = device;
    m_queueType = type;
    m_bindlessDescriptorBufferAddress = bindlessDescriptorBufferAddress;
    m_vkCmdBindDescriptorBuffersEXT = bindDescriptorBuffers;
    m_vkCmdSetDescriptorBufferOffsetsEXT = setDescriptorBufferOffsets;

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
    if (desc.type == RHIBarrierDesc::Type::Transition && desc.buffer)
    {
        auto* vkBuf = static_cast<VulkanBuffer*>(desc.buffer);

        auto convertBufferState =
            [](RHIResourceState state) -> std::pair<VkAccessFlags2, VkPipelineStageFlags2>
        {
            if (HasFlag(state, RHIResourceState::VertexBuffer))
                return {VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT};
            if (HasFlag(state, RHIResourceState::IndexBuffer))
                return {VK_ACCESS_2_INDEX_READ_BIT, VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT};
            if (HasFlag(state, RHIResourceState::CopyDest))
                return {VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT};
            if (HasFlag(state, RHIResourceState::CopySource))
                return {VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COPY_BIT};
            if (HasFlag(state, RHIResourceState::UnorderedAccess))
                return {VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
            return {VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
        };

        auto [srcAccess, srcStage] = convertBufferState(desc.stateBefore);
        auto [dstAccess, dstStage] = convertBufferState(desc.stateAfter);

        VkBufferMemoryBarrier2 bufBarrier{};
        bufBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        bufBarrier.srcStageMask = srcStage;
        bufBarrier.srcAccessMask = srcAccess;
        bufBarrier.dstStageMask = dstStage;
        bufBarrier.dstAccessMask = dstAccess;
        bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufBarrier.buffer = vkBuf->GetVkBuffer();
        bufBarrier.offset = 0;
        bufBarrier.size = VK_WHOLE_SIZE;

        VkDependencyInfo depInfo{};
        depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depInfo.bufferMemoryBarrierCount = 1;
        depInfo.pBufferMemoryBarriers = &bufBarrier;

        vkCmdPipelineBarrier2(m_cmdBuffer, &depInfo);
    }
}

// ── Viewport & Scissor ────────────────────────────────────────────────────

void VulkanCommandList::SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth)
{
    VkViewport viewport{};
    // Use negative viewport height matching DX12 NDC coordinates (Y-up clip space)
    viewport.x = x;
    viewport.y = y + h;
    viewport.width = w;
    viewport.height = -h;
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

void VulkanCommandList::SetPipeline(IRHIPipeline* pipeline)
{
    auto* vkPipeline = static_cast<VulkanPipeline*>(pipeline);
    WEST_ASSERT(vkPipeline != nullptr);
    vkCmdBindPipeline(m_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, vkPipeline->GetVkPipeline());
    m_currentPipelineLayout = vkPipeline->GetVkPipelineLayout();

    if (m_bindlessDescriptorBufferAddress != 0 && m_vkCmdBindDescriptorBuffersEXT &&
        m_vkCmdSetDescriptorBufferOffsetsEXT)
    {
        VkDescriptorBufferBindingInfoEXT bindingInfo{};
        bindingInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
        bindingInfo.address = m_bindlessDescriptorBufferAddress;
        bindingInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                            VK_BUFFER_USAGE_SAMPLER_DESCRIPTOR_BUFFER_BIT_EXT;

        m_vkCmdBindDescriptorBuffersEXT(m_cmdBuffer, 1, &bindingInfo);

        uint32_t bufferIndex = 0;
        VkDeviceSize descriptorOffset = 0;
        m_vkCmdSetDescriptorBufferOffsetsEXT(m_cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                             m_currentPipelineLayout, 0, 1,
                                             &bufferIndex, &descriptorOffset);
    }
}

void VulkanCommandList::SetPushConstants(const void* data, uint32_t sizeBytes)
{
    WEST_ASSERT(data != nullptr);
    WEST_ASSERT(sizeBytes > 0);
    WEST_ASSERT(m_currentPipelineLayout != VK_NULL_HANDLE);
    vkCmdPushConstants(m_cmdBuffer, m_currentPipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeBytes, data);
}

void VulkanCommandList::SetVertexBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset)
{
    auto* vkBuf = static_cast<VulkanBuffer*>(buffer);
    WEST_ASSERT(vkBuf != nullptr);
    VkBuffer buffers[] = {vkBuf->GetVkBuffer()};
    VkDeviceSize offsets[] = {offset};
    vkCmdBindVertexBuffers(m_cmdBuffer, slot, 1, buffers, offsets);
}

void VulkanCommandList::SetIndexBuffer(IRHIBuffer* buffer, RHIFormat format, uint64_t offset)
{
    auto* vkBuf = static_cast<VulkanBuffer*>(buffer);
    WEST_ASSERT(vkBuf != nullptr);
    VkIndexType indexType = (format == RHIFormat::R32_UINT) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
    vkCmdBindIndexBuffer(m_cmdBuffer, vkBuf->GetVkBuffer(), offset, indexType);
}

void VulkanCommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                             uint32_t firstInstance)
{
    vkCmdDraw(m_cmdBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void VulkanCommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                                    int32_t vertexOffset, uint32_t firstInstance)
{
    vkCmdDrawIndexed(m_cmdBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
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

void VulkanCommandList::CopyBuffer(IRHIBuffer* src, uint64_t srcOffset, IRHIBuffer* dst,
                                   uint64_t dstOffset, uint64_t size)
{
    auto* vkSrc = static_cast<VulkanBuffer*>(src);
    auto* vkDst = static_cast<VulkanBuffer*>(dst);
    WEST_ASSERT(vkSrc != nullptr && vkDst != nullptr);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size;
    vkCmdCopyBuffer(m_cmdBuffer, vkSrc->GetVkBuffer(), vkDst->GetVkBuffer(), 1, &copyRegion);
}

void VulkanCommandList::CopyBufferToTexture(IRHIBuffer* src, IRHITexture* dst, const RHICopyRegion& region)
{
    auto* vkSrc = static_cast<VulkanBuffer*>(src);
    auto* vkDst = static_cast<VulkanTexture*>(dst);
    WEST_ASSERT(vkSrc != nullptr && vkDst != nullptr);

    VkBufferImageCopy copy{};
    copy.bufferOffset = region.bufferOffset;
    copy.bufferRowLength = region.bufferRowLength;
    copy.bufferImageHeight = region.bufferImageHeight;
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = region.mipLevel;
    copy.imageSubresource.baseArrayLayer = region.arrayLayer;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {static_cast<int32_t>(region.texOffsetX), static_cast<int32_t>(region.texOffsetY),
                        static_cast<int32_t>(region.texOffsetZ)};
    copy.imageExtent = {region.texWidth, region.texHeight, region.texDepth};

    vkCmdCopyBufferToImage(m_cmdBuffer, vkSrc->GetVkBuffer(), vkDst->GetVkImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
}

void VulkanCommandList::WriteTimestamp(IRHIBuffer* /*queryBuffer*/, uint32_t /*index*/)
{
    // TODO(minsu): Phase 7
}

} // namespace west::rhi
