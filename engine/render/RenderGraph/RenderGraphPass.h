// =============================================================================
// WestEngine - Render
// Render Graph pass contract
// =============================================================================
#pragma once

#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/RHIEnums.h"

namespace west::render
{

class RenderGraphBuilder;
class RenderGraphContext;

class RenderGraphPass
{
public:
    virtual ~RenderGraphPass() = default;

    virtual void Setup(RenderGraphBuilder& builder) = 0;
    virtual void Execute(RenderGraphContext& context, rhi::IRHICommandList& commandList) = 0;
    [[nodiscard]] virtual bool HasSideEffects() const
    {
        return false;
    }
    [[nodiscard]] virtual rhi::RHIQueueType GetQueueType() const = 0;
    [[nodiscard]] virtual const char* GetDebugName() const = 0;
};

} // namespace west::render
