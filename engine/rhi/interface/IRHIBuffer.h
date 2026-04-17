// =============================================================================
// WestEngine - RHI Interface
// Abstract buffer resource
// =============================================================================
#pragma once

#include "rhi/interface/RHIDescriptors.h"
#include "rhi/interface/RHIEnums.h"

namespace west::rhi
{

class IRHIBuffer
{
public:
    virtual ~IRHIBuffer() = default;

    virtual const RHIBufferDesc& GetDesc() const = 0;

    /// Map CPU-accessible memory.
    /// @pre memoryType != GPULocal
    /// @return Mapped pointer. nullptr on failure.
    virtual void* Map() = 0;
    virtual void Unmap() = 0;

    /// Bindless pool index. InvalidBindlessIndex if not registered.
    virtual BindlessIndex GetBindlessIndex() const = 0;
};

} // namespace west::rhi
