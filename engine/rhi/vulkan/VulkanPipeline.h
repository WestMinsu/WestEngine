// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan pipeline — minimal graphics pipeline for Phase 2
// =============================================================================
#pragma once

#include "rhi/interface/IRHIPipeline.h"
#include "rhi/vulkan/VulkanHelpers.h"

namespace west::rhi
{

struct RHIComputePipelineDesc;
struct RHIGraphicsPipelineDesc;

class VulkanDevice;

class VulkanPipeline final : public IRHIPipeline
{
public:
    VulkanPipeline() = default;
    ~VulkanPipeline() override;

    void Initialize(VkDevice device, const RHIGraphicsPipelineDesc& desc, VkFormat swapChainFormat,
                    VkDescriptorSetLayout bindlessSetLayout);
    void Initialize(VkDevice device, const RHIComputePipelineDesc& desc, VkDescriptorSetLayout bindlessSetLayout);

    // ── IRHIPipeline interface ────────────────────────────────────────
    RHIPipelineType GetType() const override { return m_type; }
    uint64_t GetPSOHash() const override { return m_psoHash; }

    // ── Internal ──────────────────────────────────────────────────────
    void SetOwnerDevice(VulkanDevice* device) { m_ownerDevice = device; }
    VkPipeline GetVkPipeline() const { return m_pipeline; }
    VkPipelineLayout GetVkPipelineLayout() const { return m_pipelineLayout; }
    VkPipelineBindPoint GetVkBindPoint() const
    {
        return m_type == RHIPipelineType::Compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    }

private:
    VulkanDevice* m_ownerDevice = nullptr;
    VkDevice m_device = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    RHIPipelineType m_type = RHIPipelineType::Graphics;
    uint64_t m_psoHash = 0;
};

} // namespace west::rhi
