// =============================================================================
// WestEngine - Render
// Compute frustum culling pass that emits indexed indirect draw arguments
// =============================================================================
#pragma once

#include "render/RenderGraph/RenderGraph.h"

namespace west::shader
{
class PSOCache;
} // namespace west::shader

namespace west::render
{

class GPUDrivenCullingPass final : public RenderGraphPass
{
public:
    GPUDrivenCullingPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend);

    void Configure(BufferHandle frameBuffer, BufferHandle drawBuffer, BufferHandle indirectArgs,
                   BufferHandle indirectCount, uint32_t drawCount);

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList) override;
    [[nodiscard]] rhi::RHIQueueType GetQueueType() const override
    {
        return rhi::RHIQueueType::Graphics;
    }
    [[nodiscard]] const char* GetDebugName() const override
    {
        return "GPUDrivenCullingPass";
    }

private:
    void CreatePipeline();

    rhi::IRHIDevice& m_device;
    shader::PSOCache& m_psoCache;
    rhi::RHIBackend m_backend = rhi::RHIBackend::DX12;
    rhi::IRHIPipeline* m_pipeline = nullptr;
    BufferHandle m_frameBuffer{};
    BufferHandle m_drawBuffer{};
    BufferHandle m_indirectArgs{};
    BufferHandle m_indirectCount{};
    uint32_t m_drawCount = 0;
};

} // namespace west::render
