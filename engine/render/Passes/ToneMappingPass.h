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
    enum class ToneMappingOperator : uint32_t
    {
        None = 0,
        Reinhard = 1,
        ACES = 2,
        Uncharted2 = 3,
        GranTurismo = 4,
        Lottes = 5,
        Exponential = 6,
        ReinhardExtended = 7,
        Luminance = 8,
        Hable = 9,
    };

    enum class DebugView : uint32_t
    {
        Off = 0,
        ToneMappingSplit = 1,
        ColorChannels = 2,
        PostSplit = 3,
    };

    enum class DebugChannel : uint32_t
    {
        All = 0,
        Red = 1,
        Green = 2,
        Blue = 3,
        Alpha = 4,
        Luminance = 5,
    };

    struct PostSettings
    {
        ToneMappingOperator toneMappingOperator = ToneMappingOperator::ACES;
        float exposure = 1.0f;
        float gamma = 2.2f;
        float maxWhite = 4.0f;
        float contrast = 1.03f;
        float brightness = 0.0f;
        float saturation = 1.02f;
        float vibrance = 0.08f;
        float vignetteStrength = 0.12f;
        float vignetteRadius = 0.82f;
        float filmGrainStrength = 0.015f;
        float chromaticAberration = 0.08f;
        bool FXAAEnabled = true;
        DebugView debugView = DebugView::Off;
        DebugChannel debugChannel = DebugChannel::All;
        float debugSplit = 0.5f;
    };

    ToneMappingPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                    rhi::IRHISampler* sampler);

    void Configure(TextureHandle sceneColor, TextureHandle backBuffer);
    void SetPostSettings(const PostSettings& settings);

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
    PostSettings m_postSettings{};
};

} // namespace west::render
