// =============================================================================
// WestEngine - RHI Interface
// Abstract queue — command submission with synchronization
// =============================================================================
#pragma once

#include "rhi/interface/RHIDescriptors.h"
#include "rhi/interface/RHIEnums.h"

namespace west::rhi
{

class IRHIQueue
{
public:
    virtual ~IRHIQueue() = default;

    virtual void Submit(const RHISubmitInfo& info) = 0;
    virtual RHIQueueType GetType() const = 0;
};

} // namespace west::rhi
