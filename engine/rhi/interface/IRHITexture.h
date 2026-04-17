// =============================================================================
// WestEngine - RHI Interface
// Abstract texture resource
// =============================================================================
#pragma once

#include "rhi/interface/RHIDescriptors.h"
#include "rhi/interface/RHIEnums.h"

namespace west::rhi
{

class IRHITexture
{
public:
    virtual ~IRHITexture() = default;

    virtual const RHITextureDesc& GetDesc() const = 0;
    virtual BindlessIndex GetBindlessIndex() const = 0;
};

} // namespace west::rhi
