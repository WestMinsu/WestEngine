// =============================================================================
// WestEngine - Editor
// Render Graph wrapper for the Dear ImGui overlay
// =============================================================================
#include "editor/ImGuiPass.h"

#include "core/Assert.h"
#include "editor/ImGuiRenderer.h"
#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/IRHITexture.h"

namespace west::editor
{

void ImGuiPass::Configure(render::TextureHandle backBuffer)
{
    m_backBuffer = backBuffer;
}

void ImGuiPass::Setup(render::RenderGraphBuilder& builder)
{
    WEST_ASSERT(m_backBuffer.IsValid());
    builder.WriteTexture(m_backBuffer, rhi::RHIResourceState::RenderTarget);
}

void ImGuiPass::Execute(render::RenderGraphContext& context, rhi::IRHICommandList& commandList)
{
    WEST_ASSERT(m_backBuffer.IsValid());
    rhi::IRHITexture* backBuffer = context.GetTexture(m_backBuffer);
    WEST_ASSERT(backBuffer != nullptr);

    if (!m_renderer.HasDrawData())
    {
        return;
    }

    rhi::RHIColorAttachment colorAttachment{};
    colorAttachment.texture = backBuffer;
    colorAttachment.loadOp = rhi::RHILoadOp::Load;
    colorAttachment.storeOp = rhi::RHIStoreOp::Store;

    rhi::RHIRenderPassDesc renderPassDesc{};
    renderPassDesc.colorAttachments = {&colorAttachment, 1};
    renderPassDesc.debugName = GetDebugName();

    commandList.BeginRenderPass(renderPassDesc);
    m_renderer.Render(commandList, backBuffer->GetDesc().width, backBuffer->GetDesc().height);
    commandList.EndRenderPass();
}

} // namespace west::editor
