// =============================================================================
// WestEngine - Render
// Compute frustum culling pass that emits indexed indirect draw arguments
// =============================================================================
#include "render/Passes/GPUDrivenCullingPass.h"

#include "core/Assert.h"
#include "generated/ShaderMetadata.h"
#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHIPipeline.h"
#include "shader/PSOCache.h"
#include "shader/ShaderCompiler.h"

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

struct GPUDrivenCullingPushConstants
{
    DescriptorHandle frameData;
    DescriptorHandle drawData;
    DescriptorHandle indirectArgs;
    DescriptorHandle indirectCount;
    uint32_t drawCount = 0;
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
};

static_assert(sizeof(GPUDrivenCullingPushConstants) == shader::metadata::GPUDrivenCulling::PushConstantSizeBytes);

} // namespace

GPUDrivenCullingPass::GPUDrivenCullingPass(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend)
    : m_device(device)
    , m_psoCache(psoCache)
    , m_backend(backend)
{
    CreatePipeline();
}

void GPUDrivenCullingPass::Configure(BufferHandle frameBuffer, BufferHandle drawBuffer, BufferHandle indirectArgs,
                                     BufferHandle indirectCount, uint32_t drawCount)
{
    m_frameBuffer = frameBuffer;
    m_drawBuffer = drawBuffer;
    m_indirectArgs = indirectArgs;
    m_indirectCount = indirectCount;
    m_drawCount = drawCount;
}

void GPUDrivenCullingPass::Setup(RenderGraphBuilder& builder)
{
    WEST_ASSERT(m_frameBuffer.IsValid());
    WEST_ASSERT(m_drawBuffer.IsValid());
    WEST_ASSERT(m_indirectArgs.IsValid());
    WEST_ASSERT(m_indirectCount.IsValid());
    WEST_ASSERT(m_drawCount > 0);

    builder.ReadBuffer(m_frameBuffer, rhi::RHIResourceState::ShaderResource);
    builder.ReadBuffer(m_drawBuffer, rhi::RHIResourceState::ShaderResource);
    builder.ReadWriteBuffer(m_indirectArgs, rhi::RHIResourceState::UnorderedAccess);
    builder.ReadWriteBuffer(m_indirectCount, rhi::RHIResourceState::UnorderedAccess);
}

void GPUDrivenCullingPass::Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList)
{
    WEST_ASSERT(m_pipeline != nullptr);
    WEST_ASSERT(m_drawCount > 0);

    rhi::IRHIBuffer* frameBuffer = context.GetBuffer(m_frameBuffer);
    rhi::IRHIBuffer* drawBuffer = context.GetBuffer(m_drawBuffer);
    rhi::IRHIBuffer* indirectArgs = context.GetBuffer(m_indirectArgs);
    rhi::IRHIBuffer* indirectCount = context.GetBuffer(m_indirectCount);
    WEST_ASSERT(frameBuffer != nullptr);
    WEST_ASSERT(drawBuffer != nullptr);
    WEST_ASSERT(indirectArgs != nullptr);
    WEST_ASSERT(indirectCount != nullptr);

    const rhi::BindlessIndex frameBufferIndex = frameBuffer->GetBindlessIndex();
    const rhi::BindlessIndex drawBufferIndex = drawBuffer->GetBindlessIndex();
    const rhi::BindlessIndex indirectArgsIndex = indirectArgs->GetBindlessIndex();
    const rhi::BindlessIndex indirectCountIndex = indirectCount->GetBindlessIndex();
    WEST_ASSERT(frameBufferIndex != rhi::kInvalidBindlessIndex);
    WEST_ASSERT(drawBufferIndex != rhi::kInvalidBindlessIndex);
    WEST_ASSERT(indirectArgsIndex != rhi::kInvalidBindlessIndex);
    WEST_ASSERT(indirectCountIndex != rhi::kInvalidBindlessIndex);

    GPUDrivenCullingPushConstants pushConstants{};
    pushConstants.frameData.index = frameBufferIndex;
    pushConstants.drawData.index = drawBufferIndex;
    pushConstants.indirectArgs.index = indirectArgsIndex;
    pushConstants.indirectCount.index = indirectCountIndex;
    pushConstants.drawCount = m_drawCount;

    commandList.SetPipeline(m_pipeline);
    commandList.SetPushConstants(&pushConstants, sizeof(pushConstants));

    const uint32_t groupCountX =
        (m_drawCount + shader::metadata::GPUDrivenCulling::WorkgroupSizeX - 1u) /
        shader::metadata::GPUDrivenCulling::WorkgroupSizeX;
    commandList.Dispatch(groupCountX, 1, 1);
}

void GPUDrivenCullingPass::CreatePipeline()
{
    std::vector<uint8_t> computeShader;

    if (m_backend == rhi::RHIBackend::DX12)
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("GPUDrivenCulling.cs.dxil", computeShader),
                   "Failed to load GPUDrivenCulling DXIL compute shader");
    }
    else
    {
        WEST_CHECK(shader::ShaderCompiler::LoadBytecode("GPUDrivenCulling.cs.spv", computeShader),
                   "Failed to load GPUDrivenCulling SPIR-V compute shader");
    }

    rhi::RHIComputePipelineDesc pipelineDesc{};
    pipelineDesc.computeShader = std::span<const uint8_t>(computeShader.data(), computeShader.size());
    pipelineDesc.pushConstantSizeBytes = shader::metadata::GPUDrivenCulling::PushConstantSizeBytes;
    pipelineDesc.debugName = "GPUDrivenCullingPipeline";

    m_pipeline = m_psoCache.GetOrCreateComputePipeline(m_device, pipelineDesc);
    WEST_ASSERT(m_pipeline != nullptr);
}

} // namespace west::render
