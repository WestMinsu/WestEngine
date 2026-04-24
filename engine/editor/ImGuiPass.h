// =============================================================================
// WestEngine - Editor
// Render Graph wrapper for the Dear ImGui overlay
// =============================================================================
#pragma once

#include "render/RenderGraph/RenderGraph.h"

namespace west::editor
{

class ImGuiRenderer;

class ImGuiPass final : public render::RenderGraphPass
{
public:
    explicit ImGuiPass(ImGuiRenderer& renderer)
        : m_renderer(renderer)
    {
    }

    void Configure(render::TextureHandle backBuffer);

    void Setup(render::RenderGraphBuilder& builder) override;
    void Execute(render::RenderGraphContext& context, rhi::IRHICommandList& commandList) override;
    [[nodiscard]] rhi::RHIQueueType GetQueueType() const override
    {
        return rhi::RHIQueueType::Graphics;
    }
    [[nodiscard]] const char* GetDebugName() const override
    {
        return "ImGuiPass";
    }

private:
    ImGuiRenderer& m_renderer;
    render::TextureHandle m_backBuffer{};
};

} // namespace west::editor
