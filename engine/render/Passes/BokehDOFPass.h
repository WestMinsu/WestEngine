// =============================================================================
// WestEngine - Render
// Full-screen Bokeh depth of field pass consuming HDR scene color and G-Buffer
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

class BokehDOFPass final : public RenderGraphPass
{
public:
    struct Settings
    {
        float focusRangeScale = 0.18f;
        float maxBlurRadius = 5.5f;
        float intensity = 0.7f;
        float highlightBoost = 1.15f;
        float foregroundBias = 0.35f;
    };

    BokehDOFPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                 rhi::IRHISampler* sampler);

    void Configure(TextureHandle sceneColor, TextureHandle worldPosition, TextureHandle output);
    void SetFrameData(BufferHandle frameBuffer);
    void SetSettings(const Settings& settings);

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList) override;
    [[nodiscard]] rhi::RHIQueueType GetQueueType() const override
    {
        return rhi::RHIQueueType::Graphics;
    }
    [[nodiscard]] const char* GetDebugName() const override
    {
        return "BokehDOFPass";
    }

private:
    void CreatePipeline();

    rhi::IRHIDevice& m_device;
    shader::PSOCache& m_psoCache;
    rhi::RHIBackend m_backend = rhi::RHIBackend::DX12;
    rhi::IRHISampler* m_sampler = nullptr;
    rhi::IRHIPipeline* m_pipeline = nullptr;
    BufferHandle m_frameBuffer{};
    TextureHandle m_sceneColor{};
    TextureHandle m_worldPosition{};
    TextureHandle m_output{};
    Settings m_settings{};
};

} // namespace west::render
