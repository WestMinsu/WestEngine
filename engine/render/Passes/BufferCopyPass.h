// =============================================================================
// WestEngine - Render
// Simple buffer-to-buffer copy pass for Render Graph integration
// =============================================================================
#pragma once

#include "render/RenderGraph/RenderGraph.h"

namespace west::render
{

class BufferCopyPass final : public RenderGraphPass
{
public:
    explicit BufferCopyPass(const char* debugName)
        : m_debugName(debugName)
    {
    }

    void Configure(BufferHandle source, BufferHandle destination, uint64_t sizeBytes);

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList) override;
    [[nodiscard]] bool HasSideEffects() const override
    {
        return true;
    }
    [[nodiscard]] rhi::RHIQueueType GetQueueType() const override
    {
        return rhi::RHIQueueType::Graphics;
    }
    [[nodiscard]] const char* GetDebugName() const override
    {
        return m_debugName;
    }

private:
    const char* m_debugName = "BufferCopyPass";
    BufferHandle m_source{};
    BufferHandle m_destination{};
    uint64_t m_sizeBytes = 0;
};

} // namespace west::render
