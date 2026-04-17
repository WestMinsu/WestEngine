// =============================================================================
// WestEngine - RHI Interface
// Abstract sampler
// =============================================================================
#pragma once

#include "rhi/interface/RHIDescriptors.h"
#include "rhi/interface/RHIEnums.h"

namespace west::rhi
{

class IRHISampler
{
public:
    virtual ~IRHISampler() = default;

    virtual const RHISamplerDesc& GetDesc() const = 0;
    virtual BindlessIndex GetBindlessIndex() const = 0;
};

} // namespace west::rhi
