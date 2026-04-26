// =============================================================================
// WestEngine - Render
// Directional-light shadow map pass
#pragma once

#include "render/Passes/SceneDraw.h"
#include "render/RenderGraph/RenderGraph.h"

#include <span>
#include <vector>

namespace west::shader
{
class PSOCache;
} // namespace west::shader

namespace west::rhi
{
class IRHIBuffer;
class IRHISampler;
} // namespace west::rhi

namespace west::render
{

class ShadowMapPass final : public RenderGraphPass
{
public:
    ShadowMapPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                  rhi::IRHISampler* sampler);

    void ConfigureTarget(TextureHandle shadowMap);
    void SetMaterialSampler(rhi::IRHISampler* sampler);
    void SetSharedGeometry(BufferHandle sharedVertexBuffer, BufferHandle sharedIndexBuffer);
    void SetSceneData(std::span<const StaticMeshDrawItem> draws, BufferHandle frameBuffer,
                      BufferHandle materialBuffer);

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList) override;
    [[nodiscard]] rhi::RHIQueueType GetQueueType() const override
    {
        return rhi::RHIQueueType::Graphics;
    }
    [[nodiscard]] const char* GetDebugName() const override
    {
        return "ShadowMapPass";
    }

private:
    void CreatePipeline();

    rhi::IRHIDevice& m_device;
    shader::PSOCache& m_psoCache;
    rhi::RHIBackend m_backend = rhi::RHIBackend::DX12;
    rhi::IRHIPipeline* m_pipeline = nullptr;
    rhi::IRHISampler* m_sampler = nullptr;
    TextureHandle m_shadowMap{};
    BufferHandle m_frameBuffer{};
    BufferHandle m_materialBuffer{};
    BufferHandle m_sharedVertexBuffer{};
    BufferHandle m_sharedIndexBuffer{};
    std::vector<StaticMeshDrawItem> m_draws;
};

} // namespace west::render
