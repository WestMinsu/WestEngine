// =============================================================================
// WestEngine - Render
// Tone mapping pass reading HDR scene color and writing the swapchain
// =============================================================================
#include "render/Passes/ToneMappingPass.h"

#include "core/Assert.h"
#include "generated/ShaderMetadata.h"
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

struct ToneMappingPushConstants
{
    DescriptorHandle texture;
    DescriptorHandle sampler;
    float colorGrading[4] = {};
    float postEffects0[4] = {};
    float postEffects1[4] = {};
    uint32_t controls[4] = {};
};

static_assert(sizeof(ToneMappingPushConstants) == shader::metadata::ToneMapping::PushConstantSizeBytes);

} // namespace

ToneMappingPass::ToneMappingPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                                 rhi::IRHISampler* sampler)
    : m_device(device)
    , m_psoCache(psoCache)
    , m_backend(backend)
    , m_sampler(sampler)
{
    CreatePipeline();
}

void ToneMappingPass::Configure(TextureHandle sceneColor, TextureHandle backBuffer)
{
    m_sceneColor = sceneColor;
    m_backBuffer = backBuffer;
}

void ToneMappingPass::SetPostSettings(const PostSettings& settings)
{
    m_postSettings = settings;
}

void ToneMappingPass::Setup(RenderGraphBuilder& builder)
{
    WEST_ASSERT(m_sceneColor.IsValid());
    WEST_ASSERT(m_backBuffer.IsValid());
    builder.ReadTexture(m_sceneColor, rhi::RHIResourceState::ShaderResource,
                        rhi::RHIPipelineStage::PixelShader);
    builder.WriteTexture(m_backBuffer, rhi::RHIResourceState::RenderTarget,
                         rhi::RHIPipelineStage::ColorAttachmentOutput);
}

void ToneMappingPass::Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList)
{
    WEST_ASSERT(m_pipeline != nullptr);
    WEST_ASSERT(m_sampler != nullptr);

    rhi::IRHITexture* sceneColor = context.GetTexture(m_sceneColor);
    rhi::IRHITexture* backBuffer = context.GetTexture(m_backBuffer);
    WEST_ASSERT(sceneColor != nullptr);
    WEST_ASSERT(backBuffer != nullptr);

    rhi::RHIColorAttachment colorAttachment{};
    colorAttachment.texture = backBuffer;
    colorAttachment.loadOp = rhi::RHILoadOp::DontCare;
    colorAttachment.storeOp = rhi::RHIStoreOp::Store;

    rhi::RHIRenderPassDesc renderPassDesc{};
    renderPassDesc.colorAttachments = {&colorAttachment, 1};
    renderPassDesc.debugName = GetDebugName();

    commandList.BeginRenderPass(renderPassDesc);
    commandList.SetViewport(0.0f, 0.0f, static_cast<float>(backBuffer->GetDesc().width),
                            static_cast<float>(backBuffer->GetDesc().height));
    commandList.SetScissor(0, 0, backBuffer->GetDesc().width, backBuffer->GetDesc().height);

    ToneMappingPushConstants pushConstants{};
    pushConstants.texture.index = sceneColor->GetBindlessIndex();
    pushConstants.sampler.index = m_sampler->GetBindlessIndex();
    pushConstants.colorGrading[0] = m_postSettings.exposure;
    pushConstants.colorGrading[1] = m_postSettings.gamma;
    pushConstants.colorGrading[2] = m_postSettings.maxWhite;
    pushConstants.colorGrading[3] = m_postSettings.debugSplit;
    pushConstants.postEffects0[0] = m_postSettings.vibrance;
    pushConstants.postEffects0[1] = m_postSettings.contrast;
    pushConstants.postEffects0[2] = m_postSettings.brightness;
    pushConstants.postEffects0[3] = m_postSettings.saturation;
    pushConstants.postEffects1[0] = m_postSettings.vignetteStrength;
    pushConstants.postEffects1[1] = m_postSettings.vignetteRadius;
    pushConstants.postEffects1[2] = m_postSettings.filmGrainStrength;
    pushConstants.postEffects1[3] = m_postSettings.chromaticAberration;
    pushConstants.controls[0] = static_cast<uint32_t>(m_postSettings.toneMappingOperator);
    pushConstants.controls[1] = static_cast<uint32_t>(m_postSettings.debugView);
    pushConstants.controls[2] = static_cast<uint32_t>(m_postSettings.debugChannel);
    pushConstants.controls[3] = m_postSettings.FXAAEnabled ? 1u : 0u;

    commandList.SetPipeline(m_pipeline);
    commandList.SetPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.Draw(3);
    commandList.EndRenderPass();
}

void ToneMappingPass::CreatePipeline()
{
    std::vector<uint8_t> vertexShader;
    std::vector<uint8_t> fragmentShader;

    WEST_CHECK(shader::ShaderCompiler::LoadStageBytecode(m_backend, "ToneMapping",
                                                         shader::ShaderCompiler::Stage::Vertex, vertexShader),
               "Failed to load ToneMapping vertex shader");
    WEST_CHECK(shader::ShaderCompiler::LoadStageBytecode(m_backend, "ToneMapping",
                                                         shader::ShaderCompiler::Stage::Fragment, fragmentShader),
               "Failed to load ToneMapping fragment shader");

    const rhi::RHIFormat colorFormat = rhi::RHIFormat::BGRA8_UNORM;

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
    pipelineDesc.pushConstantSizeBytes = shader::metadata::ToneMapping::PushConstantSizeBytes;
    pipelineDesc.debugName = "ToneMappingPipeline";

    m_pipeline = m_psoCache.GetOrCreateGraphicsPipeline(m_device, pipelineDesc);
    WEST_ASSERT(m_pipeline != nullptr);
}

} // namespace west::render
