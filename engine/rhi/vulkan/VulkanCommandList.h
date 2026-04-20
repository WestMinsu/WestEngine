// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan command list — command buffer recording
// =============================================================================
#pragma once

#include "rhi/interface/IRHICommandList.h"
#include "rhi/vulkan/VulkanHelpers.h"

namespace west::rhi
{

class VulkanCommandList final : public IRHICommandList
{
public:
    VulkanCommandList() = default;
    ~VulkanCommandList() override;

    void Initialize(VkDevice device, uint32_t queueFamilyIndex, RHIQueueType type,
                    VkDeviceAddress bindlessDescriptorBufferAddress,
                    PFN_vkCmdBindDescriptorBuffersEXT bindDescriptorBuffers,
                    PFN_vkCmdSetDescriptorBufferOffsetsEXT setDescriptorBufferOffsets);

    // ── IRHICommandList interface ─────────────────────────────────────
    void Begin() override;
    void End() override;
    void Reset() override;

    void SetPipeline(IRHIPipeline* pipeline) override;
    void SetPushConstants(const void* data, uint32_t sizeBytes) override;
    void SetVertexBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset = 0) override;
    void SetIndexBuffer(IRHIBuffer* buffer, RHIFormat format, uint64_t offset = 0) override;
    void SetViewport(float x, float y, float w, float h, float minDepth = 0.0f, float maxDepth = 1.0f) override;
    void SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h) override;

    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0,
              uint32_t firstInstance = 0) override;
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0,
                     uint32_t firstInstance = 0) override;
    void DrawIndexedIndirectCount(IRHIBuffer* argsBuffer, uint64_t argsOffset, IRHIBuffer* countBuffer,
                                  uint64_t countOffset, uint32_t maxDrawCount, uint32_t stride) override;
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) override;

    void BeginRenderPass(const RHIRenderPassDesc& desc) override;
    void EndRenderPass() override;
    void ResourceBarrier(const RHIBarrierDesc& desc) override;

    void CopyBuffer(IRHIBuffer* src, uint64_t srcOffset, IRHIBuffer* dst, uint64_t dstOffset, uint64_t size) override;
    void CopyBufferToTexture(IRHIBuffer* src, IRHITexture* dst, const RHICopyRegion& region) override;
    void WriteTimestamp(IRHIBuffer* queryBuffer, uint32_t index) override;

    RHIQueueType GetQueueType() const override
    {
        return m_queueType;
    }

    // ── Internal ──────────────────────────────────────────────────────
    VkCommandBuffer GetVkCommandBuffer() const
    {
        return m_cmdBuffer;
    }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkCommandPool m_cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer m_cmdBuffer = VK_NULL_HANDLE;
    VkDeviceAddress m_bindlessDescriptorBufferAddress = 0;
    PFN_vkCmdBindDescriptorBuffersEXT m_vkCmdBindDescriptorBuffersEXT = nullptr;
    PFN_vkCmdSetDescriptorBufferOffsetsEXT m_vkCmdSetDescriptorBufferOffsetsEXT = nullptr;
    VkPipelineLayout m_currentPipelineLayout = VK_NULL_HANDLE;
    RHIQueueType m_queueType = RHIQueueType::Graphics;
};

} // namespace west::rhi
