// =============================================================================
// WestEngine - Render
// Screen-space ambient occlusion pass consuming the G-Buffer
// =============================================================================
#include "render/Passes/SSAOPass.h"

#include "core/Assert.h"
#include "generated/ShaderMetadata.h"
#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHIPipeline.h"
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

struct SSAOPushConstants
{
    DescriptorHandle sceneDepthTexture;
    DescriptorHandle normalRoughnessTexture;
    DescriptorHandle frameData;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
};

static_assert(sizeof(SSAOPushConstants) == shader::metadata::SSAO::PushConstantSizeBytes);

} // namespace

SSAOPass::SSAOPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend)
    : m_device(device)
    , m_psoCache(psoCache)
    , m_backend(backend)
{
    CreatePipeline();
}

void SSAOPass::ConfigureTargets(TextureHandle sceneDepth, TextureHandle normalRoughness, TextureHandle ambientOcclusion)
{
    m_sceneDepth = sceneDepth;
    m_normalRoughness = normalRoughness;
    m_ambientOcclusion = ambientOcclusion;
}

void SSAOPass::SetFrameData(BufferHandle frameBuffer)
{
    m_frameBuffer = frameBuffer;
}

void SSAOPass::Setup(RenderGraphBuilder& builder)
{
    WEST_ASSERT(m_sceneDepth.IsValid());
    WEST_ASSERT(m_normalRoughness.IsValid());
    WEST_ASSERT(m_ambientOcclusion.IsValid());
    WEST_ASSERT(m_frameBuffer.IsValid());

    constexpr rhi::RHIPipelineStage pixelStage = rhi::RHIPipelineStage::PixelShader;
    builder.ReadBuffer(m_frameBuffer, rhi::RHIResourceState::ShaderResource, pixelStage);
    builder.ReadTexture(m_sceneDepth, rhi::RHIResourceState::ShaderResource, pixelStage);
    builder.ReadTexture(m_normalRoughness, rhi::RHIResourceState::ShaderResource, pixelStage);
    builder.WriteTexture(m_ambientOcclusion, rhi::RHIResourceState::RenderTarget,
                         rhi::RHIPipelineStage::ColorAttachmentOutput);
}

void SSAOPass::Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList)
{
    WEST_ASSERT(m_pipeline != nullptr);
    WEST_ASSERT(m_frameBuffer.IsValid());

    rhi::IRHIBuffer* frameBuffer = context.GetBuffer(m_frameBuffer);
    rhi::IRHITexture* sceneDepth = context.GetTexture(m_sceneDepth);
    rhi::IRHITexture* normalRoughness = context.GetTexture(m_normalRoughness);
    rhi::IRHITexture* ambientOcclusion = context.GetTexture(m_ambientOcclusion);
    WEST_ASSERT(frameBuffer != nullptr);
    WEST_ASSERT(sceneDepth != nullptr);
    WEST_ASSERT(normalRoughness != nullptr);
    WEST_ASSERT(ambientOcclusion != nullptr);

    const rhi::BindlessIndex frameBufferIndex = frameBuffer->GetBindlessIndex();
    WEST_ASSERT(frameBufferIndex != rhi::kInvalidBindlessIndex);

    rhi::RHIColorAttachment colorAttachment{};
    colorAttachment.texture = ambientOcclusion;
    colorAttachment.loadOp = rhi::RHILoadOp::Clear;
    colorAttachment.storeOp = rhi::RHIStoreOp::Store;
    colorAttachment.clearColor[0] = 1.0f;
    colorAttachment.clearColor[1] = 1.0f;
    colorAttachment.clearColor[2] = 1.0f;
    colorAttachment.clearColor[3] = 1.0f;

    rhi::RHIRenderPassDesc renderPassDesc{};
    renderPassDesc.colorAttachments = {&colorAttachment, 1};
    renderPassDesc.debugName = GetDebugName();

    SSAOPushConstants pushConstants{};
    pushConstants.sceneDepthTexture.index = sceneDepth->GetBindlessIndex();
    pushConstants.normalRoughnessTexture.index = normalRoughness->GetBindlessIndex();
    pushConstants.frameData.index = frameBufferIndex;

    commandList.BeginRenderPass(renderPassDesc);
    commandList.SetViewport(0.0f, 0.0f, static_cast<float>(ambientOcclusion->GetDesc().width),
                            static_cast<float>(ambientOcclusion->GetDesc().height));
    commandList.SetScissor(0, 0, ambientOcclusion->GetDesc().width, ambientOcclusion->GetDesc().height);
    commandList.SetPipeline(m_pipeline);
    commandList.SetPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.Draw(3);
    commandList.EndRenderPass();
}

void SSAOPass::CreatePipeline()
{
    std::vector<uint8_t> vertexShader;
    std::vector<uint8_t> fragmentShader;

    if (m_backend == rhi::RHIBackend::DX12)
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("SSAO.vs.dxil", vertexShader),
                   "Failed to load SSAO DXIL vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("SSAO.ps.dxil", fragmentShader),
                   "Failed to load SSAO DXIL fragment shader");
    }
    else
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("SSAO.vs.spv", vertexShader),
                   "Failed to load SSAO SPIR-V vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("SSAO.ps.spv", fragmentShader),
                   "Failed to load SSAO SPIR-V fragment shader");
    }

    const rhi::RHIFormat colorFormat = rhi::RHIFormat::R16_FLOAT;

    rhi::RHIGraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = std::span<const uint8_t>(vertexShader.data(), vertexShader.size());
    pipelineDesc.fragmentShader = std::span<const uint8_t>(fragmentShader.data(), fragmentShader.size());
    pipelineDesc.vertexAttributes = {};
    pipelineDesc.vertexStride = 0;
    pipelineDesc.topology = rhi::RHIPrimitiveTopology::TriangleList;
    pipelineDesc.cullMode = rhi::RHICullMode::None;
    pipelineDesc.depthTest = false;
    pipelineDesc.depthWrite = false;
    pipelineDesc.colorFormats = {&colorFormat, 1};
    pipelineDesc.depthFormat = rhi::RHIFormat::Unknown;
    pipelineDesc.pushConstantSizeBytes = shader::metadata::SSAO::PushConstantSizeBytes;
    pipelineDesc.debugName = "SSAOPipeline";

    m_pipeline = m_psoCache.GetOrCreateGraphicsPipeline(m_device, pipelineDesc);
    WEST_ASSERT(m_pipeline != nullptr);
}

} // namespace west::render
