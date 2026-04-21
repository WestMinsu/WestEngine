// =============================================================================
// WestEngine - Render
// Forward textured quad pass writing into transient HDR scene color
// =============================================================================
#pragma once

#include "render/RenderGraph/RenderGraph.h"

#include <array>

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

class ForwardTexturedQuadPass final : public RenderGraphPass
{
public:
    ForwardTexturedQuadPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                            rhi::IRHIBuffer* quadVB, rhi::IRHIBuffer* quadIB, rhi::IRHITexture* checkerTexture,
                            rhi::IRHISampler* checkerSampler);

    void Configure(TextureHandle sceneColor, const std::array<float, 4>& clearColor);

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList) override;
    [[nodiscard]] rhi::RHIQueueType GetQueueType() const override
    {
        return rhi::RHIQueueType::Graphics;
    }
    [[nodiscard]] const char* GetDebugName() const override
    {
        return "ForwardTexturedQuadPass";
    }

private:
    void CreatePipeline();

    rhi::IRHIDevice& m_device;
    shader::PSOCache& m_psoCache;
    rhi::RHIBackend m_backend = rhi::RHIBackend::DX12;
    rhi::IRHIBuffer* m_quadVB = nullptr;
    rhi::IRHIBuffer* m_quadIB = nullptr;
    rhi::IRHITexture* m_checkerTexture = nullptr;
    rhi::IRHISampler* m_checkerSampler = nullptr;
    rhi::IRHIPipeline* m_pipeline = nullptr;
    TextureHandle m_sceneColor{};
    std::array<float, 4> m_clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
};

} // namespace west::render
