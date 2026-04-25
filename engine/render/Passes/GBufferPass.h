// =============================================================================
// WestEngine - Render
// Deferred G-Buffer pass for scene rendering
// =============================================================================
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

class GBufferPass final : public RenderGraphPass
{
public:
    GBufferPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                rhi::IRHISampler* sampler);

    void ConfigureTargets(TextureHandle worldPosition, TextureHandle normalRoughness, TextureHandle albedoMetallic,
                          TextureHandle sceneDepth);
    void SetMaterialSampler(rhi::IRHISampler* sampler);
    void SetSharedGeometry(BufferHandle sharedVertexBuffer, BufferHandle sharedIndexBuffer);
    void SetSceneData(std::span<const StaticMeshDrawItem> draws, BufferHandle frameBuffer,
                      BufferHandle materialBuffer, BufferHandle drawBuffer);
    void SetIndirectBuffers(BufferHandle indirectArgs, BufferHandle indirectCount,
                            BufferHandle sharedVertexBuffer, BufferHandle sharedIndexBuffer, uint32_t maxDrawCount);
    void DisableIndirect();

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList) override;
    [[nodiscard]] rhi::RHIQueueType GetQueueType() const override
    {
        return rhi::RHIQueueType::Graphics;
    }
    [[nodiscard]] const char* GetDebugName() const override
    {
        return "GBufferPass";
    }

private:
    void CreatePipeline();

    rhi::IRHIDevice& m_device;
    shader::PSOCache& m_psoCache;
    rhi::RHIBackend m_backend = rhi::RHIBackend::DX12;
    rhi::IRHIPipeline* m_pipeline = nullptr;
    rhi::IRHISampler* m_sampler = nullptr;
    TextureHandle m_worldPosition{};
    TextureHandle m_normalRoughness{};
    TextureHandle m_albedoMetallic{};
    TextureHandle m_sceneDepth{};
    BufferHandle m_frameBuffer{};
    BufferHandle m_materialBuffer{};
    BufferHandle m_drawBuffer{};
    BufferHandle m_indirectArgs{};
    BufferHandle m_indirectCount{};
    BufferHandle m_sharedVertexBuffer{};
    BufferHandle m_sharedIndexBuffer{};
    uint32_t m_maxDrawCount = 0;
    bool m_useIndirect = false;
    std::vector<StaticMeshDrawItem> m_draws;
};

} // namespace west::render
