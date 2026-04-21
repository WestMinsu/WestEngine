// =============================================================================
// WestEngine - Render
// Tone mapping pass reading HDR scene color and writing the swapchain
// =============================================================================
#pragma once

#include "render/RenderGraph/RenderGraph.h"

namespace west::shader
{
class PSOCache;
} // namespace west::shader

namespace west::rhi
{
class IRHISampler;
} // namespace west::rhi

namespace west::render
{

class ToneMappingPass final : public RenderGraphPass
{
public:
    ToneMappingPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                    rhi::IRHISampler* sampler);

    void Configure(TextureHandle sceneColor, TextureHandle backBuffer);

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList) override;
    [[nodiscard]] rhi::RHIQueueType GetQueueType() const override
    {
        return rhi::RHIQueueType::Graphics;
    }
    [[nodiscard]] const char* GetDebugName() const override
    {
        return "ToneMappingPass";
    }

private:
    void CreatePipeline();

    rhi::IRHIDevice& m_device;
    shader::PSOCache& m_psoCache;
    rhi::RHIBackend m_backend = rhi::RHIBackend::DX12;
    rhi::IRHISampler* m_sampler = nullptr;
    rhi::IRHIPipeline* m_pipeline = nullptr;
    TextureHandle m_sceneColor{};
    TextureHandle m_backBuffer{};
};

} // namespace west::render
