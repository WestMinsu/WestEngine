// =============================================================================
// WestEngine - Render
// Directional-light shadow map pass for // =============================================================================
#include "render/Passes/ShadowMapPass.h"

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

struct ShadowMapPushConstants
{
    DescriptorHandle frameData;
    DescriptorHandle materialData;
    DescriptorHandle sampler;
    uint32_t materialIndex = 0;
    uint32_t padding0 = 0;
    float modelMatrix[16] = {};
};

static_assert(sizeof(ShadowMapPushConstants) == shader::metadata::ShadowMap::PushConstantSizeBytes);

} // namespace

ShadowMapPass::ShadowMapPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                             rhi::IRHISampler* sampler)
    : m_device(device)
    , m_psoCache(psoCache)
    , m_backend(backend)
    , m_sampler(sampler)
{
    CreatePipeline();
}

void ShadowMapPass::ConfigureTarget(TextureHandle shadowMap)
{
    m_shadowMap = shadowMap;
}

void ShadowMapPass::SetMaterialSampler(rhi::IRHISampler* sampler)
{
    WEST_ASSERT(sampler != nullptr);
    m_sampler = sampler;
}

void ShadowMapPass::SetSceneData(std::span<const StaticMeshDrawItem> draws, BufferHandle frameBuffer,
                                 BufferHandle materialBuffer)
{
    m_draws.assign(draws.begin(), draws.end());
    m_frameBuffer = frameBuffer;
    m_materialBuffer = materialBuffer;
}

void ShadowMapPass::Setup(RenderGraphBuilder& builder)
{
    WEST_ASSERT(m_shadowMap.IsValid());
    WEST_ASSERT(m_frameBuffer.IsValid());
    WEST_ASSERT(m_materialBuffer.IsValid());

    builder.ReadBuffer(m_frameBuffer, rhi::RHIResourceState::ShaderResource);
    builder.ReadBuffer(m_materialBuffer, rhi::RHIResourceState::ShaderResource);
    builder.WriteTexture(m_shadowMap, rhi::RHIResourceState::DepthStencilWrite);
}

void ShadowMapPass::Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList)
{
    WEST_ASSERT(m_pipeline != nullptr);
    WEST_ASSERT(m_frameBuffer.IsValid());
    WEST_ASSERT(m_materialBuffer.IsValid());
    WEST_ASSERT(m_sampler != nullptr);

    rhi::IRHIBuffer* frameBuffer = context.GetBuffer(m_frameBuffer);
    rhi::IRHIBuffer* materialBuffer = context.GetBuffer(m_materialBuffer);
    rhi::IRHITexture* shadowMap = context.GetTexture(m_shadowMap);
    WEST_ASSERT(frameBuffer != nullptr);
    WEST_ASSERT(materialBuffer != nullptr);
    WEST_ASSERT(shadowMap != nullptr);

    rhi::RHIDepthAttachment depthAttachment{};
    depthAttachment.texture = shadowMap;
    depthAttachment.loadOp = rhi::RHILoadOp::Clear;
    depthAttachment.storeOp = rhi::RHIStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;
    depthAttachment.clearStencil = 0;

    rhi::RHIRenderPassDesc renderPassDesc{};
    renderPassDesc.colorAttachments = {};
    renderPassDesc.depthAttachment = depthAttachment;
    renderPassDesc.debugName = GetDebugName();

    commandList.BeginRenderPass(renderPassDesc);
    commandList.SetViewport(0.0f, 0.0f, static_cast<float>(shadowMap->GetDesc().width),
                            static_cast<float>(shadowMap->GetDesc().height));
    commandList.SetScissor(0, 0, shadowMap->GetDesc().width, shadowMap->GetDesc().height);
    commandList.SetPipeline(m_pipeline);

    const rhi::BindlessIndex frameBufferIndex = frameBuffer->GetBindlessIndex();
    const rhi::BindlessIndex materialBufferIndex = materialBuffer->GetBindlessIndex();
    const rhi::BindlessIndex samplerIndex = m_sampler->GetBindlessIndex();
    WEST_ASSERT(frameBufferIndex != rhi::kInvalidBindlessIndex);
    WEST_ASSERT(materialBufferIndex != rhi::kInvalidBindlessIndex);
    WEST_ASSERT(samplerIndex != rhi::kInvalidBindlessIndex);

    for (const StaticMeshDrawItem& draw : m_draws)
    {
        WEST_ASSERT(draw.vertexBuffer != nullptr);
        WEST_ASSERT(draw.indexBuffer != nullptr);
        WEST_ASSERT(draw.indexCount > 0);

        ShadowMapPushConstants pushConstants{};
        pushConstants.frameData.index = frameBufferIndex;
        pushConstants.materialData.index = materialBufferIndex;
        pushConstants.sampler.index = samplerIndex;
        pushConstants.materialIndex = draw.materialIndex;
        for (size_t i = 0; i < draw.modelMatrix.size(); ++i)
        {
            pushConstants.modelMatrix[i] = draw.modelMatrix[i];
        }

        commandList.SetPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.SetVertexBuffer(0, draw.vertexBuffer, draw.vertexOffsetBytes);
        commandList.SetIndexBuffer(draw.indexBuffer, rhi::RHIFormat::R32_UINT, draw.indexOffsetBytes);
        commandList.DrawIndexed(draw.indexCount);
    }

    commandList.EndRenderPass();
}

void ShadowMapPass::CreatePipeline()
{
    std::vector<uint8_t> vertexShader;
    std::vector<uint8_t> fragmentShader;

    if (m_backend == rhi::RHIBackend::DX12)
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("ShadowMap.vs.dxil", vertexShader),
                   "Failed to load ShadowMap DXIL vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("ShadowMap.ps.dxil", fragmentShader),
                   "Failed to load ShadowMap DXIL fragment shader");
    }
    else
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("ShadowMap.vs.spv", vertexShader),
                   "Failed to load ShadowMap SPIR-V vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("ShadowMap.ps.spv", fragmentShader),
                   "Failed to load ShadowMap SPIR-V fragment shader");
    }

    rhi::RHIVertexAttribute vertexAttributes[] = {
        {"POSITION", rhi::RHIFormat::RGB32_FLOAT, 0},
        {"TEXCOORD", rhi::RHIFormat::RG32_FLOAT, 24},
    };

    rhi::RHIGraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = std::span<const uint8_t>(vertexShader.data(), vertexShader.size());
    pipelineDesc.fragmentShader = std::span<const uint8_t>(fragmentShader.data(), fragmentShader.size());
    pipelineDesc.vertexAttributes = vertexAttributes;
    pipelineDesc.vertexStride = 32;
    pipelineDesc.topology = rhi::RHIPrimitiveTopology::TriangleList;
    // Match the main deferred scene path and HonglabVulkan's Bistro baseline: avoid dropping
    // floor/ground triangles whose winding is inconsistent in the source OBJ content.
    pipelineDesc.cullMode = rhi::RHICullMode::None;
    pipelineDesc.depthTest = true;
    pipelineDesc.depthWrite = true;
    pipelineDesc.colorFormats = {};
    pipelineDesc.depthFormat = rhi::RHIFormat::D32_FLOAT;
    pipelineDesc.pushConstantSizeBytes = shader::metadata::ShadowMap::PushConstantSizeBytes;
    pipelineDesc.debugName = "DirectionalShadowMapPipeline";

    m_pipeline = m_psoCache.GetOrCreateGraphicsPipeline(m_device, pipelineDesc);
    WEST_ASSERT(m_pipeline != nullptr);
}

} // namespace west::render
