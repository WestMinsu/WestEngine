// =============================================================================
// WestEngine - Render
// Simple buffer-to-buffer copy pass for Render Graph integration
// =============================================================================
#include "render/Passes/BufferCopyPass.h"

#include "core/Assert.h"
#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHICommandList.h"

namespace west::render
{

void BufferCopyPass::Configure(BufferHandle source, BufferHandle destination, uint64_t sizeBytes)
{
    m_source = source;
    m_destination = destination;
    m_sizeBytes = sizeBytes;
}

void BufferCopyPass::Setup(RenderGraphBuilder& builder)
{
    WEST_ASSERT(m_source.IsValid());
    WEST_ASSERT(m_destination.IsValid());
    WEST_ASSERT(m_sizeBytes > 0);

    builder.ReadBuffer(m_source, rhi::RHIResourceState::CopySource, rhi::RHIPipelineStage::Copy);
    builder.WriteBuffer(m_destination, rhi::RHIResourceState::CopyDest, rhi::RHIPipelineStage::Copy);
}

void BufferCopyPass::Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList)
{
    rhi::IRHIBuffer* source = context.GetBuffer(m_source);
    rhi::IRHIBuffer* destination = context.GetBuffer(m_destination);
    WEST_ASSERT(source != nullptr);
    WEST_ASSERT(destination != nullptr);
    WEST_ASSERT(m_sizeBytes <= source->GetDesc().sizeBytes);
    WEST_ASSERT(m_sizeBytes <= destination->GetDesc().sizeBytes);

    commandList.CopyBuffer(source, 0, destination, 0, m_sizeBytes);
}

} // namespace west::render
