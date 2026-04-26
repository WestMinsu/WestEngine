// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan command list implementation — Phase 1: barrier + ClearColor
// =============================================================================
#include "rhi/vulkan/VulkanCommandList.h"

#include "rhi/vulkan/VulkanBuffer.h"
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanPipeline.h"
#include "rhi/vulkan/VulkanTimestampQueryPool.h"
#include "rhi/vulkan/VulkanTexture.h"
#include "rhi/common/FormatConversion.h"

#include <tuple>
#include <vector>

namespace west::rhi
{

namespace
{

[[nodiscard]] bool IsDepthFormat(RHIFormat format)
{
    return format == RHIFormat::D16_UNORM ||
           format == RHIFormat::D32_FLOAT ||
           format == RHIFormat::D24_UNORM_S8_UINT ||
           format == RHIFormat::D32_FLOAT_S8_UINT;
}

[[nodiscard]] bool IsStencilFormat(RHIFormat format)
{
    return format == RHIFormat::D24_UNORM_S8_UINT || format == RHIFormat::D32_FLOAT_S8_UINT;
}

[[nodiscard]] VkImageAspectFlags GetFormatAspectMask(RHIFormat format)
{
    if (IsDepthFormat(format))
    {
        VkImageAspectFlags aspects = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (IsStencilFormat(format))
        {
            aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
        }
        return aspects;
    }

    return VK_IMAGE_ASPECT_COLOR_BIT;
}

[[nodiscard]] std::tuple<VkImageLayout, VkAccessFlags2, VkPipelineStageFlags2> ConvertTextureState(
    RHIResourceState state, RHIFormat format)
{
    WEST_CHECK(state != RHIResourceState::Common,
               "Vulkan texture barriers require a concrete state; Common is ambiguous");

    if (HasFlag(state, RHIResourceState::RenderTarget))
        return {VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT};
    if (HasFlag(state, RHIResourceState::Present))
        return {VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT};
    if (HasFlag(state, RHIResourceState::ShaderResource))
        return {VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_2_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
    if (HasFlag(state, RHIResourceState::CopyDest))
        return {VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COPY_BIT};
    if (HasFlag(state, RHIResourceState::CopySource))
        return {VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_2_COPY_BIT};
    if (HasFlag(state, RHIResourceState::DepthStencilWrite))
        return {VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT};
    if (HasFlag(state, RHIResourceState::DepthStencilRead))
        return {VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,
                VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT};
    if (HasFlag(state, RHIResourceState::UnorderedAccess))
        return {VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};

    WEST_CHECK(state == RHIResourceState::Undefined, "Unsupported Vulkan texture resource state: {}",
               static_cast<uint32_t>(state));
    return {VK_IMAGE_LAYOUT_UNDEFINED, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
}

[[nodiscard]] std::pair<VkAccessFlags2, VkPipelineStageFlags2> ConvertBufferState(RHIResourceState state)
{
    if (HasFlag(state, RHIResourceState::VertexBuffer))
        return {VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT, VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT};
    if (HasFlag(state, RHIResourceState::IndexBuffer))
        return {VK_ACCESS_2_INDEX_READ_BIT, VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT};
    if (HasFlag(state, RHIResourceState::ConstantBuffer))
        return {VK_ACCESS_2_UNIFORM_READ_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
    if (HasFlag(state, RHIResourceState::ShaderResource))
        return {VK_ACCESS_2_SHADER_READ_BIT, VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
    if (HasFlag(state, RHIResourceState::CopyDest))
        return {VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT};
    if (HasFlag(state, RHIResourceState::CopySource))
        return {VK_ACCESS_2_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_2_COPY_BIT};
    if (HasFlag(state, RHIResourceState::UnorderedAccess))
        return {VK_ACCESS_2_SHADER_WRITE_BIT | VK_ACCESS_2_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT};
    if (HasFlag(state, RHIResourceState::IndirectArgument))
        return {VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT};

    return {VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT};
}

[[nodiscard]] VkPipelineStageFlags2 ConvertPipelineStageMask(RHIPipelineStage stageMask,
                                                             VkPipelineStageFlags2 fallback)
{
    if (stageMask == RHIPipelineStage::Auto)
    {
        return fallback;
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

    return result != 0 ? result : fallback;
}

[[nodiscard]] VkImageAspectFlags GetTextureAspectMask(const VulkanTexture* texture)
{
    return GetFormatAspectMask(texture->GetDesc().format);
}

void AppendBarrier(const RHIBarrierDesc& desc, std::vector<VkMemoryBarrier2>& memoryBarriers,
                   std::vector<VkBufferMemoryBarrier2>& bufferBarriers,
                   std::vector<VkImageMemoryBarrier2>& imageBarriers)
{
    if (desc.type == RHIBarrierDesc::Type::Transition && desc.texture)
    {
        auto* vkTex = static_cast<VulkanTexture*>(desc.texture);
        auto [oldLayout, srcAccess, srcStage] = ConvertTextureState(desc.stateBefore, vkTex->GetDesc().format);
        auto [newLayout, dstAccess, dstStage] = ConvertTextureState(desc.stateAfter, vkTex->GetDesc().format);

        VkImageMemoryBarrier2 imageBarrier{};
        imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        imageBarrier.srcStageMask = ConvertPipelineStageMask(desc.srcStageMask, srcStage);
        imageBarrier.srcAccessMask = srcAccess;
        imageBarrier.dstStageMask = ConvertPipelineStageMask(desc.dstStageMask, dstStage);
        imageBarrier.dstAccessMask = dstAccess;
        imageBarrier.oldLayout = oldLayout;
        imageBarrier.newLayout = newLayout;
        imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        imageBarrier.image = vkTex->GetVkImage();
        imageBarrier.subresourceRange = {GetTextureAspectMask(vkTex), 0, vkTex->GetDesc().mipLevels,
                                         0, vkTex->GetDesc().arrayLayers};
        imageBarriers.push_back(imageBarrier);
        return;
    }

    if (desc.type == RHIBarrierDesc::Type::Transition && desc.buffer)
    {
        auto* vkBuf = static_cast<VulkanBuffer*>(desc.buffer);
        auto [srcAccess, srcStage] = ConvertBufferState(desc.stateBefore);
        auto [dstAccess, dstStage] = ConvertBufferState(desc.stateAfter);

        VkBufferMemoryBarrier2 bufferBarrier{};
        bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
        bufferBarrier.srcStageMask = ConvertPipelineStageMask(desc.srcStageMask, srcStage);
        bufferBarrier.srcAccessMask = srcAccess;
        bufferBarrier.dstStageMask = ConvertPipelineStageMask(desc.dstStageMask, dstStage);
        bufferBarrier.dstAccessMask = dstAccess;
        bufferBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        bufferBarrier.buffer = vkBuf->GetVkBuffer();
        bufferBarrier.offset = 0;
        bufferBarrier.size = VK_WHOLE_SIZE;
        bufferBarriers.push_back(bufferBarrier);
        return;
    }

    if (desc.type == RHIBarrierDesc::Type::Aliasing)
    {
        VkMemoryBarrier2 memoryBarrier{};
        memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        memoryBarrier.srcStageMask =
            ConvertPipelineStageMask(desc.srcStageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        memoryBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT;
        memoryBarrier.dstStageMask =
            ConvertPipelineStageMask(desc.dstStageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        memoryBarrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
        memoryBarriers.push_back(memoryBarrier);
        return;
    }

    if (desc.type == RHIBarrierDesc::Type::UAV)
    {
        VkMemoryBarrier2 memoryBarrier{};
        memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
        memoryBarrier.srcStageMask =
            ConvertPipelineStageMask(desc.srcStageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        memoryBarrier.srcAccessMask = VK_ACCESS_2_SHADER_WRITE_BIT;
        memoryBarrier.dstStageMask =
            ConvertPipelineStageMask(desc.dstStageMask, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
        memoryBarrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT;
        memoryBarriers.push_back(memoryBarrier);
    }
}

} // namespace

VulkanCommandList::~VulkanCommandList()
{
    if (m_cmdPool && m_device)
    {
        VkDevice device = m_device;
        VkCommandPool commandPool = m_cmdPool;
        if (m_ownerDevice && m_ownerDevice->GetVkDevice() != VK_NULL_HANDLE)
        {
            m_ownerDevice->EnqueueDeferredDeletion(
                [device, commandPool]()
                {
                    vkDestroyCommandPool(device, commandPool, nullptr);
                },
                m_ownerDevice->GetCurrentFrameFenceValue());
        }
        else
        {
            vkDestroyCommandPool(device, commandPool, nullptr);
        }

        m_cmdPool = VK_NULL_HANDLE;
        m_cmdBuffer = VK_NULL_HANDLE;
    }
}

void VulkanCommandList::Initialize(VulkanDevice* ownerDevice, VkDevice device, uint32_t queueFamilyIndex,
                                   RHIQueueType type,
                                   VkDeviceAddress bindlessDescriptorBufferAddress,
                                   PFN_vkCmdBindDescriptorBuffersEXT bindDescriptorBuffers,
                                   PFN_vkCmdSetDescriptorBufferOffsetsEXT setDescriptorBufferOffsets)
{
    WEST_CHECK(ownerDevice != nullptr, "VulkanCommandList::Initialize requires an owning device");
    m_device = device;
    m_ownerDevice = ownerDevice;
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
    ResourceBarriers(std::span<const RHIBarrierDesc>(&desc, 1));
}

void VulkanCommandList::ResourceBarriers(std::span<const RHIBarrierDesc> descs)
{
    if (descs.empty())
    {
        return;
    }

    std::vector<VkMemoryBarrier2> memoryBarriers;
    std::vector<VkBufferMemoryBarrier2> bufferBarriers;
    std::vector<VkImageMemoryBarrier2> imageBarriers;
    memoryBarriers.reserve(descs.size());
    bufferBarriers.reserve(descs.size());
    imageBarriers.reserve(descs.size());

    for (const RHIBarrierDesc& desc : descs)
    {
        AppendBarrier(desc, memoryBarriers, bufferBarriers, imageBarriers);
    }

    if (memoryBarriers.empty() && bufferBarriers.empty() && imageBarriers.empty())
    {
        return;
    }

    VkDependencyInfo depInfo{};
    depInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    depInfo.memoryBarrierCount = static_cast<uint32_t>(memoryBarriers.size());
    depInfo.pMemoryBarriers = memoryBarriers.data();
    depInfo.bufferMemoryBarrierCount = static_cast<uint32_t>(bufferBarriers.size());
    depInfo.pBufferMemoryBarriers = bufferBarriers.data();
    depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(imageBarriers.size());
    depInfo.pImageMemoryBarriers = imageBarriers.data();

    vkCmdPipelineBarrier2(m_cmdBuffer, &depInfo);
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
    colorAttachments.resize(desc.colorAttachments.size());

    VkExtent2D renderExtent{};
    bool hasRenderExtent = false;
    auto noteAttachmentExtent = [&](const RHITextureDesc& textureDesc)
    {
        VkExtent2D extent{textureDesc.width, textureDesc.height};
        if (!hasRenderExtent)
        {
            renderExtent = extent;
            hasRenderExtent = true;
            return;
        }

        WEST_CHECK(renderExtent.width == extent.width && renderExtent.height == extent.height,
                   "VulkanCommandList::BeginRenderPass attachments must have matching extents");
    };

    for (size_t attachmentIndex = 0; attachmentIndex < desc.colorAttachments.size(); ++attachmentIndex)
    {
        const auto& attach = desc.colorAttachments[attachmentIndex];
        VkRenderingAttachmentInfo& colorInfo = colorAttachments[attachmentIndex];
        colorInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        colorInfo.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorInfo.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;

        if (!attach.texture)
        {
            continue;
        }

        auto* vkTex = static_cast<VulkanTexture*>(attach.texture);
        WEST_CHECK(vkTex != nullptr, "VulkanCommandList::BeginRenderPass received a non-Vulkan color texture");
        noteAttachmentExtent(vkTex->GetDesc());

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
    }

    VkRenderingAttachmentInfo depthAttachmentInfo{};
    VkRenderingAttachmentInfo* depthAttachmentPtr = nullptr;
    if (desc.depthAttachment.texture)
    {
        auto* vkDepth = static_cast<VulkanTexture*>(desc.depthAttachment.texture);
        WEST_CHECK(vkDepth != nullptr, "VulkanCommandList::BeginRenderPass received a non-Vulkan depth texture");
        noteAttachmentExtent(vkDepth->GetDesc());

        depthAttachmentInfo.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depthAttachmentInfo.imageView = vkDepth->GetVkImageView();
        depthAttachmentInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depthAttachmentInfo.storeOp = (desc.depthAttachment.storeOp == RHIStoreOp::Store)
                                          ? VK_ATTACHMENT_STORE_OP_STORE
                                          : VK_ATTACHMENT_STORE_OP_DONT_CARE;

        if (desc.depthAttachment.loadOp == RHILoadOp::Clear)
        {
            depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachmentInfo.clearValue.depthStencil = {
                desc.depthAttachment.clearDepth,
                desc.depthAttachment.clearStencil,
            };
        }
        else if (desc.depthAttachment.loadOp == RHILoadOp::Load)
        {
            depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        }
        else
        {
            depthAttachmentInfo.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }

        depthAttachmentPtr = &depthAttachmentInfo;
    }

    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.offset = {0, 0};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = static_cast<uint32_t>(colorAttachments.size());
    renderingInfo.pColorAttachments = colorAttachments.data();
    renderingInfo.pDepthAttachment = depthAttachmentPtr;
    renderingInfo.pStencilAttachment =
        (depthAttachmentPtr && IsStencilFormat(desc.depthAttachment.texture->GetDesc().format)) ? depthAttachmentPtr
                                                                                                : nullptr;

    WEST_CHECK(hasRenderExtent, "VulkanCommandList::BeginRenderPass requires at least one attachment");
    renderingInfo.renderArea.extent = renderExtent;

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
    WEST_CHECK(vkPipeline != nullptr, "VulkanCommandList::SetPipeline received a null pipeline");
    m_currentPipelineBindPoint = vkPipeline->GetVkBindPoint();
    vkCmdBindPipeline(m_cmdBuffer, m_currentPipelineBindPoint, vkPipeline->GetVkPipeline());
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
        m_vkCmdSetDescriptorBufferOffsetsEXT(m_cmdBuffer, m_currentPipelineBindPoint,
                                             m_currentPipelineLayout, 0, 1,
                                             &bufferIndex, &descriptorOffset);
    }
}

void VulkanCommandList::SetPushConstants(const void* data, uint32_t sizeBytes)
{
    WEST_CHECK(data != nullptr, "VulkanCommandList::SetPushConstants received null data");
    WEST_CHECK(sizeBytes > 0 && (sizeBytes % sizeof(uint32_t)) == 0,
               "VulkanCommandList::SetPushConstants size must be non-zero and 4-byte aligned");
    WEST_CHECK(sizeBytes <= kMaxPushConstantSizeBytes,
               "VulkanCommandList::SetPushConstants size {} exceeds limit {}", sizeBytes, kMaxPushConstantSizeBytes);
    WEST_CHECK(m_currentPipelineLayout != VK_NULL_HANDLE,
               "VulkanCommandList::SetPushConstants requires a bound pipeline");
    vkCmdPushConstants(m_cmdBuffer, m_currentPipelineLayout, VK_SHADER_STAGE_ALL, 0, sizeBytes, data);
}

void VulkanCommandList::SetVertexBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset)
{
    auto* vkBuf = static_cast<VulkanBuffer*>(buffer);
    WEST_CHECK(vkBuf != nullptr, "VulkanCommandList::SetVertexBuffer received a null buffer");
    WEST_CHECK(offset <= vkBuf->GetDesc().sizeBytes,
               "VulkanCommandList::SetVertexBuffer offset {} exceeds buffer size {}", offset,
               vkBuf->GetDesc().sizeBytes);
    VkBuffer buffers[] = {vkBuf->GetVkBuffer()};
    VkDeviceSize offsets[] = {offset};
    vkCmdBindVertexBuffers(m_cmdBuffer, slot, 1, buffers, offsets);
}

void VulkanCommandList::SetIndexBuffer(IRHIBuffer* buffer, RHIFormat format, uint64_t offset)
{
    auto* vkBuf = static_cast<VulkanBuffer*>(buffer);
    WEST_CHECK(vkBuf != nullptr, "VulkanCommandList::SetIndexBuffer received a null buffer");
    WEST_CHECK(format == RHIFormat::R32_UINT,
               "VulkanCommandList::SetIndexBuffer requires R32_UINT format");
    WEST_CHECK(offset <= vkBuf->GetDesc().sizeBytes,
               "VulkanCommandList::SetIndexBuffer offset {} exceeds buffer size {}", offset,
               vkBuf->GetDesc().sizeBytes);
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

void VulkanCommandList::DrawIndexedIndirectCount(IRHIBuffer* argsBuffer, uint64_t argsOffset,
                                                 IRHIBuffer* countBuffer, uint64_t countOffset,
                                                 uint32_t maxDrawCount, uint32_t stride)
{
    WEST_CHECK(argsBuffer != nullptr, "VulkanCommandList::DrawIndexedIndirectCount received null args buffer");
    WEST_CHECK(countBuffer != nullptr, "VulkanCommandList::DrawIndexedIndirectCount received null count buffer");
    WEST_CHECK(stride == sizeof(VkDrawIndexedIndirectCommand),
               "VulkanCommandList::DrawIndexedIndirectCount stride {} is unsupported", stride);

    auto* vkArgsBuffer = static_cast<VulkanBuffer*>(argsBuffer);
    auto* vkCountBuffer = static_cast<VulkanBuffer*>(countBuffer);
    WEST_CHECK(vkArgsBuffer != nullptr && vkCountBuffer != nullptr,
               "VulkanCommandList::DrawIndexedIndirectCount received a non-Vulkan buffer");
    WEST_CHECK(argsOffset <= vkArgsBuffer->GetDesc().sizeBytes &&
                   static_cast<uint64_t>(maxDrawCount) * stride <= vkArgsBuffer->GetDesc().sizeBytes - argsOffset,
               "Vulkan indirect args range exceeds buffer size");
    WEST_CHECK(countOffset <= vkCountBuffer->GetDesc().sizeBytes &&
                   sizeof(uint32_t) <= vkCountBuffer->GetDesc().sizeBytes - countOffset,
               "Vulkan indirect count range exceeds buffer size");

    vkCmdDrawIndexedIndirectCount(m_cmdBuffer, vkArgsBuffer->GetVkBuffer(), argsOffset,
                                  vkCountBuffer->GetVkBuffer(), countOffset,
                                  maxDrawCount, stride);
}

void VulkanCommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    vkCmdDispatch(m_cmdBuffer, groupCountX, groupCountY, groupCountZ);
}

void VulkanCommandList::CopyBuffer(IRHIBuffer* src, uint64_t srcOffset, IRHIBuffer* dst,
                                   uint64_t dstOffset, uint64_t size)
{
    auto* vkSrc = static_cast<VulkanBuffer*>(src);
    auto* vkDst = static_cast<VulkanBuffer*>(dst);
    WEST_CHECK(vkSrc != nullptr && vkDst != nullptr, "VulkanCommandList::CopyBuffer received a null buffer");
    WEST_CHECK(srcOffset <= vkSrc->GetDesc().sizeBytes && size <= vkSrc->GetDesc().sizeBytes - srcOffset,
               "VulkanCommandList::CopyBuffer source range exceeds buffer size");
    WEST_CHECK(dstOffset <= vkDst->GetDesc().sizeBytes && size <= vkDst->GetDesc().sizeBytes - dstOffset,
               "VulkanCommandList::CopyBuffer destination range exceeds buffer size");

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
    WEST_CHECK(vkSrc != nullptr && vkDst != nullptr,
               "VulkanCommandList::CopyBufferToTexture received a null resource");

    const RHITextureDesc& desc = vkDst->GetDesc();
    WEST_CHECK(region.texWidth > 0 && region.texHeight > 0 && region.texDepth > 0,
               "VulkanCommandList::CopyBufferToTexture requires a non-empty copy extent");
    WEST_CHECK(region.mipLevel < desc.mipLevels && region.arrayLayer < desc.arrayLayers,
               "VulkanCommandList::CopyBufferToTexture subresource is out of range");
    WEST_CHECK(region.texOffsetX + region.texWidth <= desc.width &&
                   region.texOffsetY + region.texHeight <= desc.height &&
                   region.texOffsetZ + region.texDepth <= desc.depth,
               "VulkanCommandList::CopyBufferToTexture destination region exceeds texture bounds");
    WEST_CHECK(region.bufferOffset < vkSrc->GetDesc().sizeBytes,
               "VulkanCommandList::CopyBufferToTexture source offset exceeds buffer size");

    VkBufferImageCopy copy{};
    copy.bufferOffset = region.bufferOffset;
    copy.bufferRowLength = region.bufferRowLength;
    copy.bufferImageHeight = region.bufferImageHeight;
    copy.imageSubresource.aspectMask = GetFormatAspectMask(desc.format);
    copy.imageSubresource.mipLevel = region.mipLevel;
    copy.imageSubresource.baseArrayLayer = region.arrayLayer;
    copy.imageSubresource.layerCount = 1;
    copy.imageOffset = {static_cast<int32_t>(region.texOffsetX), static_cast<int32_t>(region.texOffsetY),
                        static_cast<int32_t>(region.texOffsetZ)};
    copy.imageExtent = {region.texWidth, region.texHeight, region.texDepth};

    vkCmdCopyBufferToImage(m_cmdBuffer, vkSrc->GetVkBuffer(), vkDst->GetVkImage(),
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
}

void VulkanCommandList::ResetTimestampQueries(IRHITimestampQueryPool* queryPool, uint32_t firstQuery,
                                              uint32_t queryCount)
{
    if (queryCount == 0)
    {
        return;
    }

    auto* vkQueryPool = static_cast<VulkanTimestampQueryPool*>(queryPool);
    WEST_CHECK(vkQueryPool != nullptr, "VulkanCommandList::ResetTimestampQueries received a null query pool");
    WEST_CHECK(firstQuery <= vkQueryPool->GetDesc().queryCount &&
                   queryCount <= vkQueryPool->GetDesc().queryCount - firstQuery,
               "VulkanCommandList::ResetTimestampQueries query range is out of bounds");

    vkCmdResetQueryPool(m_cmdBuffer, vkQueryPool->GetVkQueryPool(), firstQuery, queryCount);
}

void VulkanCommandList::WriteTimestamp(IRHITimestampQueryPool* queryPool, uint32_t index)
{
    auto* vkQueryPool = static_cast<VulkanTimestampQueryPool*>(queryPool);
    WEST_CHECK(vkQueryPool != nullptr, "VulkanCommandList::WriteTimestamp received a null query pool");
    WEST_CHECK(index < vkQueryPool->GetDesc().queryCount,
               "VulkanCommandList::WriteTimestamp query index {} is out of range", index);

    vkCmdWriteTimestamp2(m_cmdBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                         vkQueryPool->GetVkQueryPool(), index);
}

void VulkanCommandList::ResolveTimestampQueries(IRHITimestampQueryPool* /*queryPool*/, uint32_t /*firstQuery*/,
                                                uint32_t /*queryCount*/)
{
    // Vulkan query results are read directly after the frame fence is complete.
}

} // namespace west::rhi
