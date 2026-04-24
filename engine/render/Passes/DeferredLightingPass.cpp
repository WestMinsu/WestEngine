// =============================================================================
// WestEngine - Render
// Deferred lighting pass consuming the G-Buffer
// =============================================================================
#include "render/Passes/DeferredLightingPass.h"

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

struct DeferredLightingPushConstants
{
    DescriptorHandle worldPositionTexture;
    DescriptorHandle normalRoughnessTexture;
    DescriptorHandle albedoMetallicTexture;
    DescriptorHandle shadowMapTexture;
    DescriptorHandle ambientOcclusionTexture;
    DescriptorHandle prefilteredEnvironmentTexture;
    DescriptorHandle irradianceEnvironmentTexture;
    DescriptorHandle brdfLutTexture;
    DescriptorHandle sceneSampler;
    DescriptorHandle shadowSampler;
    DescriptorHandle iblSampler;
    DescriptorHandle frameData;
};

static_assert(sizeof(DeferredLightingPushConstants) == shader::metadata::DeferredLighting::PushConstantSizeBytes);

} // namespace

DeferredLightingPass::DeferredLightingPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache,
                                           rhi::RHIBackend backend, rhi::IRHISampler* sceneSampler,
                                           rhi::IRHISampler* shadowSampler, rhi::IRHISampler* iblSampler)
    : m_device(device)
    , m_psoCache(psoCache)
    , m_backend(backend)
    , m_sceneSampler(sceneSampler)
    , m_shadowSampler(shadowSampler)
    , m_iblSampler(iblSampler)
{
    CreatePipeline();
}

void DeferredLightingPass::ConfigureTargets(TextureHandle worldPosition, TextureHandle normalRoughness,
                                            TextureHandle albedoMetallic, TextureHandle shadowMap,
                                            TextureHandle ambientOcclusion, TextureHandle sceneColor)
{
    m_worldPosition = worldPosition;
    m_normalRoughness = normalRoughness;
    m_albedoMetallic = albedoMetallic;
    m_shadowMap = shadowMap;
    m_ambientOcclusion = ambientOcclusion;
    m_sceneColor = sceneColor;
}

void DeferredLightingPass::SetFrameData(BufferHandle frameBuffer)
{
    m_frameBuffer = frameBuffer;
}

void DeferredLightingPass::SetIBLTextures(TextureHandle prefilteredEnvironment,
                                          TextureHandle irradianceEnvironment,
                                          TextureHandle brdfLut)
{
    m_prefilteredEnvironment = prefilteredEnvironment;
    m_irradianceEnvironment = irradianceEnvironment;
    m_brdfLut = brdfLut;
}

void DeferredLightingPass::Setup(RenderGraphBuilder& builder)
{
    WEST_ASSERT(m_worldPosition.IsValid());
    WEST_ASSERT(m_normalRoughness.IsValid());
    WEST_ASSERT(m_albedoMetallic.IsValid());
    WEST_ASSERT(m_shadowMap.IsValid());
    WEST_ASSERT(m_ambientOcclusion.IsValid());
    WEST_ASSERT(m_sceneColor.IsValid());
    WEST_ASSERT(m_frameBuffer.IsValid());
    WEST_ASSERT(m_prefilteredEnvironment.IsValid());
    WEST_ASSERT(m_irradianceEnvironment.IsValid());
    WEST_ASSERT(m_brdfLut.IsValid());

    builder.ReadBuffer(m_frameBuffer, rhi::RHIResourceState::ShaderResource);
    builder.ReadTexture(m_worldPosition, rhi::RHIResourceState::ShaderResource);
    builder.ReadTexture(m_normalRoughness, rhi::RHIResourceState::ShaderResource);
    builder.ReadTexture(m_albedoMetallic, rhi::RHIResourceState::ShaderResource);
    builder.ReadTexture(m_shadowMap, rhi::RHIResourceState::ShaderResource);
    builder.ReadTexture(m_ambientOcclusion, rhi::RHIResourceState::ShaderResource);
    builder.ReadTexture(m_prefilteredEnvironment, rhi::RHIResourceState::ShaderResource);
    builder.ReadTexture(m_irradianceEnvironment, rhi::RHIResourceState::ShaderResource);
    builder.ReadTexture(m_brdfLut, rhi::RHIResourceState::ShaderResource);
    builder.WriteTexture(m_sceneColor, rhi::RHIResourceState::RenderTarget);
}

void DeferredLightingPass::Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList)
{
    WEST_ASSERT(m_pipeline != nullptr);
    WEST_ASSERT(m_sceneSampler != nullptr);
    WEST_ASSERT(m_shadowSampler != nullptr);
    WEST_ASSERT(m_iblSampler != nullptr);
    WEST_ASSERT(m_frameBuffer.IsValid());
    WEST_ASSERT(m_prefilteredEnvironment.IsValid());
    WEST_ASSERT(m_irradianceEnvironment.IsValid());
    WEST_ASSERT(m_brdfLut.IsValid());

    rhi::IRHIBuffer* frameBuffer = context.GetBuffer(m_frameBuffer);
    rhi::IRHITexture* worldPosition = context.GetTexture(m_worldPosition);
    rhi::IRHITexture* normalRoughness = context.GetTexture(m_normalRoughness);
    rhi::IRHITexture* albedoMetallic = context.GetTexture(m_albedoMetallic);
    rhi::IRHITexture* shadowMap = context.GetTexture(m_shadowMap);
    rhi::IRHITexture* ambientOcclusion = context.GetTexture(m_ambientOcclusion);
    rhi::IRHITexture* prefilteredEnvironment = context.GetTexture(m_prefilteredEnvironment);
    rhi::IRHITexture* irradianceEnvironment = context.GetTexture(m_irradianceEnvironment);
    rhi::IRHITexture* brdfLut = context.GetTexture(m_brdfLut);
    rhi::IRHITexture* sceneColor = context.GetTexture(m_sceneColor);
    WEST_ASSERT(frameBuffer != nullptr);
    WEST_ASSERT(worldPosition != nullptr);
    WEST_ASSERT(normalRoughness != nullptr);
    WEST_ASSERT(albedoMetallic != nullptr);
    WEST_ASSERT(shadowMap != nullptr);
    WEST_ASSERT(ambientOcclusion != nullptr);
    WEST_ASSERT(prefilteredEnvironment != nullptr);
    WEST_ASSERT(irradianceEnvironment != nullptr);
    WEST_ASSERT(brdfLut != nullptr);
    WEST_ASSERT(sceneColor != nullptr);

    rhi::RHIColorAttachment colorAttachment{};
    colorAttachment.texture = sceneColor;
    colorAttachment.loadOp = rhi::RHILoadOp::Clear;
    colorAttachment.storeOp = rhi::RHIStoreOp::Store;

    rhi::RHIRenderPassDesc renderPassDesc{};
    renderPassDesc.colorAttachments = {&colorAttachment, 1};
    renderPassDesc.debugName = GetDebugName();

    commandList.BeginRenderPass(renderPassDesc);
    commandList.SetViewport(0.0f, 0.0f, static_cast<float>(sceneColor->GetDesc().width),
                            static_cast<float>(sceneColor->GetDesc().height));
    commandList.SetScissor(0, 0, sceneColor->GetDesc().width, sceneColor->GetDesc().height);

    const rhi::BindlessIndex frameBufferIndex = frameBuffer->GetBindlessIndex();
    WEST_ASSERT(frameBufferIndex != rhi::kInvalidBindlessIndex);

    DeferredLightingPushConstants pushConstants{};
    pushConstants.worldPositionTexture.index = worldPosition->GetBindlessIndex();
    pushConstants.normalRoughnessTexture.index = normalRoughness->GetBindlessIndex();
    pushConstants.albedoMetallicTexture.index = albedoMetallic->GetBindlessIndex();
    pushConstants.shadowMapTexture.index = shadowMap->GetBindlessIndex();
    pushConstants.ambientOcclusionTexture.index = ambientOcclusion->GetBindlessIndex();
    pushConstants.prefilteredEnvironmentTexture.index = prefilteredEnvironment->GetBindlessIndex();
    pushConstants.irradianceEnvironmentTexture.index = irradianceEnvironment->GetBindlessIndex();
    pushConstants.brdfLutTexture.index = brdfLut->GetBindlessIndex();
    pushConstants.sceneSampler.index = m_sceneSampler->GetBindlessIndex();
    pushConstants.shadowSampler.index = m_shadowSampler->GetBindlessIndex();
    pushConstants.iblSampler.index = m_iblSampler->GetBindlessIndex();
    pushConstants.frameData.index = frameBufferIndex;

    commandList.SetPipeline(m_pipeline);
    commandList.SetPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.Draw(3);
    commandList.EndRenderPass();
}

void DeferredLightingPass::CreatePipeline()
{
    std::vector<uint8_t> vertexShader;
    std::vector<uint8_t> fragmentShader;

    if (m_backend == rhi::RHIBackend::DX12)
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("DeferredLighting.vs.dxil", vertexShader),
                   "Failed to load DeferredLighting DXIL vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("DeferredLighting.ps.dxil", fragmentShader),
                   "Failed to load DeferredLighting DXIL fragment shader");
    }
    else
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("DeferredLighting.vs.spv", vertexShader),
                   "Failed to load DeferredLighting SPIR-V vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("DeferredLighting.ps.spv", fragmentShader),
                   "Failed to load DeferredLighting SPIR-V fragment shader");
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
    pipelineDesc.pushConstantSizeBytes = shader::metadata::DeferredLighting::PushConstantSizeBytes;
    pipelineDesc.debugName = "DeferredLightingPipeline";

    m_pipeline = m_psoCache.GetOrCreateGraphicsPipeline(m_device, pipelineDesc);
    WEST_ASSERT(m_pipeline != nullptr);
}

} // namespace west::render
