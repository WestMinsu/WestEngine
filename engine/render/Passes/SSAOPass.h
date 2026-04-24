// =============================================================================
// WestEngine - Render
// Screen-space ambient occlusion pass consuming the G-Buffer
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
} // namespace west::rhi

namespace west::render
{

class SSAOPass final : public RenderGraphPass
{
public:
    SSAOPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend);

    void ConfigureTargets(TextureHandle sceneDepth, TextureHandle normalRoughness, TextureHandle ambientOcclusion);
    void SetFrameData(BufferHandle frameBuffer);

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList) override;
    [[nodiscard]] rhi::RHIQueueType GetQueueType() const override
    {
        return rhi::RHIQueueType::Graphics;
    }
    [[nodiscard]] const char* GetDebugName() const override
    {
        return "SSAOPass";
    }

private:
    void CreatePipeline();

    rhi::IRHIDevice& m_device;
    shader::PSOCache& m_psoCache;
    rhi::RHIBackend m_backend = rhi::RHIBackend::DX12;
    rhi::IRHIPipeline* m_pipeline = nullptr;
    BufferHandle m_frameBuffer{};
    TextureHandle m_sceneDepth{};
    TextureHandle m_normalRoughness{};
    TextureHandle m_ambientOcclusion{};
};

} // namespace west::render
