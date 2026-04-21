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

void ToneMappingPass::Setup(RenderGraphBuilder& builder)
{
    WEST_ASSERT(m_sceneColor.IsValid());
    WEST_ASSERT(m_backBuffer.IsValid());
    builder.ReadTexture(m_sceneColor, rhi::RHIResourceState::ShaderResource);
    builder.WriteTexture(m_backBuffer, rhi::RHIResourceState::RenderTarget);
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

    commandList.SetPipeline(m_pipeline);
    commandList.SetPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.Draw(3);
    commandList.EndRenderPass();
}

void ToneMappingPass::CreatePipeline()
{
    std::vector<uint8_t> vertexShader;
    std::vector<uint8_t> fragmentShader;

    if (m_backend == rhi::RHIBackend::DX12)
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("ToneMapping.vs.dxil", vertexShader),
                   "Failed to load ToneMapping DXIL vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("ToneMapping.ps.dxil", fragmentShader),
                   "Failed to load ToneMapping DXIL fragment shader");
    }
    else
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("ToneMapping.vs.spv", vertexShader),
                   "Failed to load ToneMapping SPIR-V vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("ToneMapping.ps.spv", fragmentShader),
                   "Failed to load ToneMapping SPIR-V fragment shader");
    }

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
