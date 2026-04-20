// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan pipeline — minimal graphics pipeline for Phase 2
// =============================================================================
#pragma once

#include "rhi/interface/IRHIPipeline.h"
#include "rhi/vulkan/VulkanHelpers.h"

namespace west::rhi
{

struct RHIGraphicsPipelineDesc;

class VulkanPipeline final : public IRHIPipeline
{
public:
    VulkanPipeline() = default;
    ~VulkanPipeline() override;

    void Initialize(VkDevice device, const RHIGraphicsPipelineDesc& desc, VkFormat swapChainFormat,
                    VkDescriptorSetLayout bindlessSetLayout);

    // ── IRHIPipeline interface ────────────────────────────────────────
    uint64_t GetPSOHash() const override { return m_psoHash; }

    // ── Internal ──────────────────────────────────────────────────────
    VkPipeline GetVkPipeline() const { return m_pipeline; }
    VkPipelineLayout GetVkPipelineLayout() const { return m_pipelineLayout; }

private:
    VkDevice m_device = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    uint64_t m_psoHash = 0;
};

} // namespace west::rhi
