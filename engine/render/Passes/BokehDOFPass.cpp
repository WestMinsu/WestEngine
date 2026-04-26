// =============================================================================
// WestEngine - Render
// Full-screen Bokeh depth of field pass consuming HDR scene color and G-Buffer
// =============================================================================
#include "render/Passes/BokehDOFPass.h"

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

struct BokehDOFPushConstants
{
    DescriptorHandle sceneColorTexture;
    DescriptorHandle worldPositionTexture;
    DescriptorHandle frameData;
    DescriptorHandle sampler;
    float dofParams0[4] = {};
    float dofParams1[4] = {};
};

static_assert(sizeof(BokehDOFPushConstants) == shader::metadata::BokehDOF::PushConstantSizeBytes);

} // namespace

BokehDOFPass::BokehDOFPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                           rhi::IRHISampler* sampler)
    : m_device(device)
    , m_psoCache(psoCache)
    , m_backend(backend)
    , m_sampler(sampler)
{
    CreatePipeline();
}

void BokehDOFPass::Configure(TextureHandle sceneColor, TextureHandle worldPosition, TextureHandle output)
{
    m_sceneColor = sceneColor;
    m_worldPosition = worldPosition;
    m_output = output;
}

void BokehDOFPass::SetFrameData(BufferHandle frameBuffer)
{
    m_frameBuffer = frameBuffer;
}

void BokehDOFPass::SetSettings(const Settings& settings)
{
    m_settings = settings;
}

void BokehDOFPass::Setup(RenderGraphBuilder& builder)
{
    WEST_ASSERT(m_sceneColor.IsValid());
    WEST_ASSERT(m_worldPosition.IsValid());
    WEST_ASSERT(m_output.IsValid());
    WEST_ASSERT(m_frameBuffer.IsValid());

    constexpr rhi::RHIPipelineStage pixelStage = rhi::RHIPipelineStage::PixelShader;
    builder.ReadBuffer(m_frameBuffer, rhi::RHIResourceState::ShaderResource, pixelStage);
    builder.ReadTexture(m_sceneColor, rhi::RHIResourceState::ShaderResource, pixelStage);
    builder.ReadTexture(m_worldPosition, rhi::RHIResourceState::ShaderResource, pixelStage);
    builder.WriteTexture(m_output, rhi::RHIResourceState::RenderTarget,
                         rhi::RHIPipelineStage::ColorAttachmentOutput);
}

void BokehDOFPass::Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList)
{
    WEST_ASSERT(m_pipeline != nullptr);
    WEST_ASSERT(m_frameBuffer.IsValid());
    WEST_ASSERT(m_sampler != nullptr);

    rhi::IRHIBuffer* frameBuffer = context.GetBuffer(m_frameBuffer);
    rhi::IRHITexture* sceneColor = context.GetTexture(m_sceneColor);
    rhi::IRHITexture* worldPosition = context.GetTexture(m_worldPosition);
    rhi::IRHITexture* output = context.GetTexture(m_output);
    WEST_ASSERT(frameBuffer != nullptr);
    WEST_ASSERT(sceneColor != nullptr);
    WEST_ASSERT(worldPosition != nullptr);
    WEST_ASSERT(output != nullptr);

    rhi::RHIColorAttachment colorAttachment{};
    colorAttachment.texture = output;
    colorAttachment.loadOp = rhi::RHILoadOp::DontCare;
    colorAttachment.storeOp = rhi::RHIStoreOp::Store;

    rhi::RHIRenderPassDesc renderPassDesc{};
    renderPassDesc.colorAttachments = {&colorAttachment, 1};
    renderPassDesc.debugName = GetDebugName();

    BokehDOFPushConstants pushConstants{};
    pushConstants.sceneColorTexture.index = sceneColor->GetBindlessIndex();
    pushConstants.worldPositionTexture.index = worldPosition->GetBindlessIndex();
    pushConstants.frameData.index = frameBuffer->GetBindlessIndex();
    pushConstants.sampler.index = m_sampler->GetBindlessIndex();
    pushConstants.dofParams0[0] = m_settings.focusRangeScale;
    pushConstants.dofParams0[1] = m_settings.maxBlurRadius;
    pushConstants.dofParams0[2] = m_settings.intensity;
    pushConstants.dofParams0[3] = m_settings.highlightBoost;
    pushConstants.dofParams1[0] = m_settings.foregroundBias;

    commandList.BeginRenderPass(renderPassDesc);
    commandList.SetViewport(0.0f, 0.0f, static_cast<float>(output->GetDesc().width),
                            static_cast<float>(output->GetDesc().height));
    commandList.SetScissor(0, 0, output->GetDesc().width, output->GetDesc().height);
    commandList.SetPipeline(m_pipeline);
    commandList.SetPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.Draw(3);
    commandList.EndRenderPass();
}

void BokehDOFPass::CreatePipeline()
{
    std::vector<uint8_t> vertexShader;
    std::vector<uint8_t> fragmentShader;

    if (m_backend == rhi::RHIBackend::DX12)
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("BokehDOF.vs.dxil", vertexShader),
                   "Failed to load BokehDOF DXIL vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("BokehDOF.ps.dxil", fragmentShader),
                   "Failed to load BokehDOF DXIL fragment shader");
    }
    else
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("BokehDOF.vs.spv", vertexShader),
                   "Failed to load BokehDOF SPIR-V vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("BokehDOF.ps.spv", fragmentShader),
                   "Failed to load BokehDOF SPIR-V fragment shader");
    }

    const rhi::RHIFormat colorFormat = rhi::RHIFormat::RGBA16_FLOAT;

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
    pipelineDesc.pushConstantSizeBytes = shader::metadata::BokehDOF::PushConstantSizeBytes;
    pipelineDesc.debugName = "BokehDOFPipeline";

    m_pipeline = m_psoCache.GetOrCreateGraphicsPipeline(m_device, pipelineDesc);
    WEST_ASSERT(m_pipeline != nullptr);
}

} // namespace west::render
