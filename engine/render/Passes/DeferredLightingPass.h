// =============================================================================
// WestEngine - Render
// Deferred lighting pass consuming the G-Buffer
// =============================================================================
#pragma once

#include "render/RenderGraph/RenderGraph.h"

namespace west::shader
{
class PSOCache;
} // namespace west::shader

namespace west::rhi
{
class IRHIBuffer;
class IRHISampler;
class IRHITexture;
} // namespace west::rhi

namespace west::render
{

class DeferredLightingPass final : public RenderGraphPass
{
public:
    DeferredLightingPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                         rhi::IRHISampler* sceneSampler, rhi::IRHISampler* shadowSampler,
                         rhi::IRHISampler* iblSampler);

    void ConfigureTargets(TextureHandle worldPosition, TextureHandle normalRoughness, TextureHandle albedoMetallic,
                          TextureHandle shadowMap, TextureHandle ambientOcclusion, TextureHandle sceneColor);
    void SetFrameData(BufferHandle frameBuffer);
    void SetIBLTextures(TextureHandle prefilteredEnvironment, TextureHandle irradianceEnvironment,
                        TextureHandle brdfLut);

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList) override;
    [[nodiscard]] rhi::RHIQueueType GetQueueType() const override
    {
        return rhi::RHIQueueType::Graphics;
    }
    [[nodiscard]] const char* GetDebugName() const override
    {
        return "DeferredLightingPass";
    }

private:
    void CreatePipeline();

    rhi::IRHIDevice& m_device;
    shader::PSOCache& m_psoCache;
    rhi::RHIBackend m_backend = rhi::RHIBackend::DX12;
    rhi::IRHISampler* m_sceneSampler = nullptr;
    rhi::IRHISampler* m_shadowSampler = nullptr;
    rhi::IRHISampler* m_iblSampler = nullptr;
    rhi::IRHIPipeline* m_pipeline = nullptr;
    TextureHandle m_worldPosition{};
    TextureHandle m_normalRoughness{};
    TextureHandle m_albedoMetallic{};
    TextureHandle m_shadowMap{};
    TextureHandle m_ambientOcclusion{};
    TextureHandle m_sceneColor{};
    BufferHandle m_frameBuffer{};
    TextureHandle m_prefilteredEnvironment{};
    TextureHandle m_irradianceEnvironment{};
    TextureHandle m_brdfLut{};
};

} // namespace west::render
