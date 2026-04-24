// =============================================================================
// WestEngine - Render
// Deferred G-Buffer pass for scene rendering
// =============================================================================
#include "render/Passes/GBufferPass.h"

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

struct GBufferPushConstants
{
    DescriptorHandle frameData;
    DescriptorHandle materialData;
    DescriptorHandle drawData;
    DescriptorHandle sampler;
    uint32_t drawRecordIndexOverride = 0;
    uint32_t useDrawRecordIndexOverride = 0;
};

static_assert(sizeof(GBufferPushConstants) == shader::metadata::GBuffer::PushConstantSizeBytes);

} // namespace

GBufferPass::GBufferPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                         rhi::IRHISampler* sampler)
    : m_device(device), m_psoCache(psoCache), m_backend(backend), m_sampler(sampler)
{
    CreatePipeline();
}

void GBufferPass::ConfigureTargets(TextureHandle worldPosition, TextureHandle normalRoughness,
                                   TextureHandle albedoMetallic, TextureHandle sceneDepth)
{
    m_worldPosition = worldPosition;
    m_normalRoughness = normalRoughness;
    m_albedoMetallic = albedoMetallic;
    m_sceneDepth = sceneDepth;
}

void GBufferPass::SetMaterialSampler(rhi::IRHISampler* sampler)
{
    WEST_ASSERT(sampler != nullptr);
    m_sampler = sampler;
}

void GBufferPass::SetSceneData(std::span<const StaticMeshDrawItem> draws, BufferHandle frameBuffer,
                               BufferHandle materialBuffer, BufferHandle drawBuffer)
{
    m_draws.assign(draws.begin(), draws.end());
    m_frameBuffer = frameBuffer;
    m_materialBuffer = materialBuffer;
    m_drawBuffer = drawBuffer;
}

void GBufferPass::SetIndirectBuffers(BufferHandle indirectArgs, BufferHandle indirectCount,
                                     rhi::IRHIBuffer* sharedVertexBuffer, rhi::IRHIBuffer* sharedIndexBuffer,
                                     uint32_t maxDrawCount)
{
    WEST_ASSERT(indirectArgs.IsValid());
    WEST_ASSERT(indirectCount.IsValid());
    WEST_ASSERT(sharedVertexBuffer != nullptr);
    WEST_ASSERT(sharedIndexBuffer != nullptr);
    WEST_ASSERT(maxDrawCount > 0);

    m_indirectArgs = indirectArgs;
    m_indirectCount = indirectCount;
    m_sharedVertexBuffer = sharedVertexBuffer;
    m_sharedIndexBuffer = sharedIndexBuffer;
    m_maxDrawCount = maxDrawCount;
    m_useIndirect = true;
}

void GBufferPass::DisableIndirect()
{
    m_indirectArgs = {};
    m_indirectCount = {};
    m_sharedVertexBuffer = nullptr;
    m_sharedIndexBuffer = nullptr;
    m_maxDrawCount = 0;
    m_useIndirect = false;
}

void GBufferPass::Setup(RenderGraphBuilder& builder)
{
    WEST_ASSERT(m_worldPosition.IsValid());
    WEST_ASSERT(m_normalRoughness.IsValid());
    WEST_ASSERT(m_albedoMetallic.IsValid());
    WEST_ASSERT(m_sceneDepth.IsValid());
    WEST_ASSERT(m_frameBuffer.IsValid());
    WEST_ASSERT(m_materialBuffer.IsValid());
    WEST_ASSERT(m_drawBuffer.IsValid());

    builder.ReadBuffer(m_frameBuffer, rhi::RHIResourceState::ShaderResource);
    builder.ReadBuffer(m_materialBuffer, rhi::RHIResourceState::ShaderResource);
    builder.ReadBuffer(m_drawBuffer, rhi::RHIResourceState::ShaderResource);
    builder.WriteTexture(m_worldPosition, rhi::RHIResourceState::RenderTarget);
    builder.WriteTexture(m_normalRoughness, rhi::RHIResourceState::RenderTarget);
    builder.WriteTexture(m_albedoMetallic, rhi::RHIResourceState::RenderTarget);
    builder.WriteTexture(m_sceneDepth, rhi::RHIResourceState::DepthStencilWrite);

    if (m_useIndirect)
    {
        WEST_ASSERT(m_indirectArgs.IsValid());
        WEST_ASSERT(m_indirectCount.IsValid());
        builder.ReadBuffer(m_indirectArgs, rhi::RHIResourceState::IndirectArgument);
        builder.ReadBuffer(m_indirectCount, rhi::RHIResourceState::IndirectArgument);
    }
}

void GBufferPass::Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList)
{
    WEST_ASSERT(m_pipeline != nullptr);
    WEST_ASSERT(m_sampler != nullptr);

    rhi::IRHIBuffer* frameBuffer = context.GetBuffer(m_frameBuffer);
    rhi::IRHIBuffer* materialBuffer = context.GetBuffer(m_materialBuffer);
    rhi::IRHIBuffer* drawBuffer = context.GetBuffer(m_drawBuffer);
    rhi::IRHITexture* worldPosition = context.GetTexture(m_worldPosition);
    rhi::IRHITexture* normalRoughness = context.GetTexture(m_normalRoughness);
    rhi::IRHITexture* albedoMetallic = context.GetTexture(m_albedoMetallic);
    rhi::IRHITexture* sceneDepth = context.GetTexture(m_sceneDepth);
    WEST_ASSERT(frameBuffer != nullptr);
    WEST_ASSERT(materialBuffer != nullptr);
    WEST_ASSERT(drawBuffer != nullptr);
    WEST_ASSERT(worldPosition != nullptr);
    WEST_ASSERT(normalRoughness != nullptr);
    WEST_ASSERT(albedoMetallic != nullptr);
    WEST_ASSERT(sceneDepth != nullptr);

    std::array<rhi::RHIColorAttachment, 3> colorAttachments = {};
    colorAttachments[0].texture = worldPosition;
    colorAttachments[0].loadOp = rhi::RHILoadOp::Clear;
    colorAttachments[0].storeOp = rhi::RHIStoreOp::Store;
    // DeferredLighting uses worldPosition.w == 0 to detect background pixels and sample the sky cubemap.
    colorAttachments[0].clearColor[0] = 0.0f;
    colorAttachments[0].clearColor[1] = 0.0f;
    colorAttachments[0].clearColor[2] = 0.0f;
    colorAttachments[0].clearColor[3] = 0.0f;
    colorAttachments[1].texture = normalRoughness;
    colorAttachments[1].loadOp = rhi::RHILoadOp::Clear;
    colorAttachments[1].storeOp = rhi::RHIStoreOp::Store;
    colorAttachments[1].clearColor[0] = 0.0f;
    colorAttachments[1].clearColor[1] = 0.0f;
    colorAttachments[1].clearColor[2] = 0.0f;
    colorAttachments[1].clearColor[3] = 0.0f;
    colorAttachments[2].texture = albedoMetallic;
    colorAttachments[2].loadOp = rhi::RHILoadOp::Clear;
    colorAttachments[2].storeOp = rhi::RHIStoreOp::Store;
    colorAttachments[2].clearColor[0] = 0.0f;
    colorAttachments[2].clearColor[1] = 0.0f;
    colorAttachments[2].clearColor[2] = 0.0f;
    colorAttachments[2].clearColor[3] = 0.0f;

    rhi::RHIDepthAttachment depthAttachment{};
    depthAttachment.texture = sceneDepth;
    depthAttachment.loadOp = rhi::RHILoadOp::Clear;
    depthAttachment.storeOp = rhi::RHIStoreOp::Store;
    depthAttachment.clearDepth = 1.0f;
    depthAttachment.clearStencil = 0;

    rhi::RHIRenderPassDesc renderPassDesc{};
    renderPassDesc.colorAttachments =
        std::span<const rhi::RHIColorAttachment>(colorAttachments.data(), colorAttachments.size());
    renderPassDesc.depthAttachment = depthAttachment;
    renderPassDesc.debugName = GetDebugName();

    commandList.BeginRenderPass(renderPassDesc);
    commandList.SetViewport(0.0f, 0.0f, static_cast<float>(worldPosition->GetDesc().width),
                            static_cast<float>(worldPosition->GetDesc().height));
    commandList.SetScissor(0, 0, worldPosition->GetDesc().width, worldPosition->GetDesc().height);
    commandList.SetPipeline(m_pipeline);

    const rhi::BindlessIndex frameBufferIndex = frameBuffer->GetBindlessIndex();
    const rhi::BindlessIndex materialBufferIndex = materialBuffer->GetBindlessIndex();
    const rhi::BindlessIndex drawBufferIndex = drawBuffer->GetBindlessIndex();
    const rhi::BindlessIndex samplerIndex = m_sampler->GetBindlessIndex();
    WEST_ASSERT(frameBufferIndex != rhi::kInvalidBindlessIndex);
    WEST_ASSERT(materialBufferIndex != rhi::kInvalidBindlessIndex);
    WEST_ASSERT(drawBufferIndex != rhi::kInvalidBindlessIndex);
    WEST_ASSERT(samplerIndex != rhi::kInvalidBindlessIndex);

    GBufferPushConstants pushConstants{};
    pushConstants.frameData.index = frameBufferIndex;
    pushConstants.materialData.index = materialBufferIndex;
    pushConstants.drawData.index = drawBufferIndex;
    pushConstants.sampler.index = samplerIndex;

    if (m_useIndirect)
    {
        pushConstants.drawRecordIndexOverride = 0;
        pushConstants.useDrawRecordIndexOverride = 0;
        commandList.SetPushConstants(&pushConstants, sizeof(pushConstants));

        WEST_ASSERT(m_sharedVertexBuffer != nullptr);
        WEST_ASSERT(m_sharedIndexBuffer != nullptr);
        WEST_ASSERT(m_indirectArgs.IsValid());
        WEST_ASSERT(m_indirectCount.IsValid());
        WEST_ASSERT(m_maxDrawCount > 0);

        rhi::IRHIBuffer* indirectArgs = context.GetBuffer(m_indirectArgs);
        rhi::IRHIBuffer* indirectCount = context.GetBuffer(m_indirectCount);
        WEST_ASSERT(indirectArgs != nullptr);
        WEST_ASSERT(indirectCount != nullptr);

        commandList.SetVertexBuffer(0, m_sharedVertexBuffer, 0);
        commandList.SetIndexBuffer(m_sharedIndexBuffer, rhi::RHIFormat::R32_UINT, 0);
        commandList.DrawIndexedIndirectCount(indirectArgs, 0, indirectCount, 0, m_maxDrawCount,
                                             sizeof(DrawIndexedIndirectArgs));
    }
    else
    {
        for (uint32_t drawIndex = 0; drawIndex < m_draws.size(); ++drawIndex)
        {
            const StaticMeshDrawItem& draw = m_draws[drawIndex];
            WEST_ASSERT(draw.vertexBuffer != nullptr);
            WEST_ASSERT(draw.indexBuffer != nullptr);
            WEST_ASSERT(draw.indexCount > 0);

            pushConstants.drawRecordIndexOverride = drawIndex;
            pushConstants.useDrawRecordIndexOverride = 1;
            commandList.SetPushConstants(&pushConstants, sizeof(pushConstants));

            commandList.SetVertexBuffer(0, draw.vertexBuffer, draw.vertexOffsetBytes);
            commandList.SetIndexBuffer(draw.indexBuffer, rhi::RHIFormat::R32_UINT, draw.indexOffsetBytes);
            commandList.DrawIndexed(draw.indexCount, 1, 0, 0, drawIndex);
        }
    }

    commandList.EndRenderPass();
}

void GBufferPass::CreatePipeline()
{
    std::vector<uint8_t> vertexShader;
    std::vector<uint8_t> fragmentShader;

    if (m_backend == rhi::RHIBackend::DX12)
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("GBuffer.vs.dxil", vertexShader),
                   "Failed to load GBuffer DXIL vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("GBuffer.ps.dxil", fragmentShader),
                   "Failed to load GBuffer DXIL fragment shader");
    }
    else
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("GBuffer.vs.spv", vertexShader),
                   "Failed to load GBuffer SPIR-V vertex shader");
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("GBuffer.ps.spv", fragmentShader),
                   "Failed to load GBuffer SPIR-V fragment shader");
    }

    rhi::RHIVertexAttribute vertexAttributes[] = {
        {"POSITION", rhi::RHIFormat::RGB32_FLOAT, 0},
        {"NORMAL", rhi::RHIFormat::RGB32_FLOAT, 12},
        {"TEXCOORD", rhi::RHIFormat::RG32_FLOAT, 24},
    };

    const rhi::RHIFormat colorFormats[] = {
        rhi::RHIFormat::RGBA16_FLOAT,
        rhi::RHIFormat::RGBA16_FLOAT,
        rhi::RHIFormat::RGBA16_FLOAT,
    };

    rhi::RHIGraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = std::span<const uint8_t>(vertexShader.data(), vertexShader.size());
    pipelineDesc.fragmentShader = std::span<const uint8_t>(fragmentShader.data(), fragmentShader.size());
    pipelineDesc.vertexAttributes = vertexAttributes;
    pipelineDesc.vertexStride = 32;
    pipelineDesc.topology = rhi::RHIPrimitiveTopology::TriangleList;
    // Bistro's OBJ content contains winding that is not robust enough for strict back-face culling.
    // HonglabVulkan's deferred path keeps culling disabled for this asset class, so keep parity here.
    pipelineDesc.cullMode = rhi::RHICullMode::None;
    pipelineDesc.depthTest = true;
    pipelineDesc.depthWrite = true;
    pipelineDesc.colorFormats = colorFormats;
    pipelineDesc.depthFormat = rhi::RHIFormat::D32_FLOAT;
    pipelineDesc.pushConstantSizeBytes = shader::metadata::GBuffer::PushConstantSizeBytes;
    pipelineDesc.debugName = "DeferredGBufferPipeline";

    m_pipeline = m_psoCache.GetOrCreateGraphicsPipeline(m_device, pipelineDesc);
    WEST_ASSERT(m_pipeline != nullptr);
}

} // namespace west::render
