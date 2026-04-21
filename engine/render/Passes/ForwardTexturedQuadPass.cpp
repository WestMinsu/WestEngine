// =============================================================================
// WestEngine - Render
// Forward textured quad pass writing into transient HDR scene color
// =============================================================================
#include "render/Passes/ForwardTexturedQuadPass.h"

#include "core/Assert.h"
#include "generated/ShaderMetadata.h"
#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHIPipeline.h"
#include "rhi/interface/IRHISampler.h"
#include "rhi/interface/IRHITexture.h"
#include "shader/PSOCache.h"
#include "shader/ShaderCompiler.h"

#include <span>
#include <vector>

namespace west::render
{

namespace
{

struct DescriptorHandle
{
    rhi::BindlessIndex index = rhi::kInvalidBindlessIndex;
    uint32_t unused = 0;
};

struct QuadPushConstants
{
    DescriptorHandle texture;
    DescriptorHandle sampler;
};

static_assert(sizeof(QuadPushConstants) == shader::metadata::TexturedQuad::PushConstantSizeBytes);

} // namespace

ForwardTexturedQuadPass::ForwardTexturedQuadPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache,
                                                 rhi::RHIBackend backend, rhi::IRHIBuffer* quadVB,
                                                 rhi::IRHIBuffer* quadIB, rhi::IRHITexture* checkerTexture,
                                                 rhi::IRHISampler* checkerSampler)
    : m_device(device)
    , m_psoCache(psoCache)
    , m_backend(backend)
    , m_quadVB(quadVB)
    , m_quadIB(quadIB)
    , m_checkerTexture(checkerTexture)
    , m_checkerSampler(checkerSampler)
{
    CreatePipeline();
}

void ForwardTexturedQuadPass::Configure(TextureHandle sceneColor, const std::array<float, 4>& clearColor)
{
    m_sceneColor = sceneColor;
    m_clearColor = clearColor;
}

void ForwardTexturedQuadPass::Setup(RenderGraphBuilder& builder)
{
    WEST_ASSERT(m_sceneColor.IsValid());
    builder.WriteTexture(m_sceneColor, rhi::RHIResourceState::RenderTarget);
}

void ForwardTexturedQuadPass::Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList)
{
    WEST_ASSERT(m_pipeline != nullptr);
    WEST_ASSERT(m_quadVB != nullptr);
    WEST_ASSERT(m_quadIB != nullptr);
    WEST_ASSERT(m_checkerTexture != nullptr);
    WEST_ASSERT(m_checkerSampler != nullptr);

    rhi::IRHITexture* sceneColor = context.GetTexture(m_sceneColor);
    WEST_ASSERT(sceneColor != nullptr);

    rhi::RHIColorAttachment colorAttachment{};
    colorAttachment.texture = sceneColor;
    colorAttachment.loadOp = rhi::RHILoadOp::Clear;
    colorAttachment.storeOp = rhi::RHIStoreOp::Store;
    colorAttachment.clearColor[0] = m_clearColor[0];
    colorAttachment.clearColor[1] = m_clearColor[1];
    colorAttachment.clearColor[2] = m_clearColor[2];
    colorAttachment.clearColor[3] = m_clearColor[3];

    rhi::RHIRenderPassDesc renderPassDesc{};
    renderPassDesc.colorAttachments = {&colorAttachment, 1};
    renderPassDesc.debugName = GetDebugName();

    commandList.BeginRenderPass(renderPassDesc);
    commandList.SetViewport(0.0f, 0.0f, static_cast<float>(sceneColor->GetDesc().width),
                            static_cast<float>(sceneColor->GetDesc().height));
    commandList.SetScissor(0, 0, sceneColor->GetDesc().width, sceneColor->GetDesc().height);

    QuadPushConstants pushConstants{};
    pushConstants.texture.index = m_checkerTexture->GetBindlessIndex();
    pushConstants.sampler.index = m_checkerSampler->GetBindlessIndex();

    commandList.SetPipeline(m_pipeline);
    commandList.SetPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.SetVertexBuffer(0, m_quadVB);
    commandList.SetIndexBuffer(m_quadIB, rhi::RHIFormat::R32_UINT);
    commandList.DrawIndexed(6);
    commandList.EndRenderPass();
}

void ForwardTexturedQuadPass::CreatePipeline()
{
    std::vector<uint8_t> vertexShader;
    std::vector<uint8_t> fragmentShader;

    if (m_backend == rhi::RHIBackend::DX12)
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("TexturedQuad.vs.dxil", vertexShader),
                   "Failed to load TexturedQuad DXIL vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("TexturedQuad.ps.dxil", fragmentShader),
                   "Failed to load TexturedQuad DXIL fragment shader");
    }
    else
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("TexturedQuad.vs.spv", vertexShader),
                   "Failed to load TexturedQuad SPIR-V vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("TexturedQuad.ps.spv", fragmentShader),
                   "Failed to load TexturedQuad SPIR-V fragment shader");
    }

    rhi::RHIVertexAttribute vertexAttributes[] = {
        {"POSITION", rhi::RHIFormat::RGB32_FLOAT, 0},
        {"TEXCOORD", rhi::RHIFormat::RG32_FLOAT, 12},
    };

    const rhi::RHIFormat colorFormat = rhi::RHIFormat::RGBA16_FLOAT;

    rhi::RHIGraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = std::span<const uint8_t>(vertexShader.data(), vertexShader.size());
    pipelineDesc.fragmentShader = std::span<const uint8_t>(fragmentShader.data(), fragmentShader.size());
    pipelineDesc.vertexAttributes = vertexAttributes;
    pipelineDesc.vertexStride = 20;
    pipelineDesc.topology = rhi::RHIPrimitiveTopology::TriangleList;
    pipelineDesc.cullMode = rhi::RHICullMode::None;
    pipelineDesc.depthTest = false;
    pipelineDesc.depthWrite = false;
    pipelineDesc.colorFormats = {&colorFormat, 1};
    pipelineDesc.depthFormat = rhi::RHIFormat::Unknown;
    pipelineDesc.pushConstantSizeBytes = shader::metadata::TexturedQuad::PushConstantSizeBytes;
    pipelineDesc.debugName = "ForwardTexturedQuadHDR";

    m_pipeline = m_psoCache.GetOrCreateGraphicsPipeline(m_device, pipelineDesc);
    WEST_ASSERT(m_pipeline != nullptr);
}

} // namespace west::render
