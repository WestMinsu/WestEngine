// =============================================================================
// WestEngine - RHI Interface
// Backend-neutral timestamp query pool for async GPU profiling
// =============================================================================
#pragma once

#include "rhi/interface/RHIDescriptors.h"

#include <span>

namespace west::rhi
{

class IRHITimestampQueryPool
{
public:
    virtual ~IRHITimestampQueryPool() = default;

    virtual const RHITimestampQueryPoolDesc& GetDesc() const = 0;
    virtual float GetTimestampPeriodNanoseconds() const = 0;
    virtual bool ReadTimestamps(uint32_t firstQuery, uint32_t queryCount,
                                std::span<uint64_t> timestamps) = 0;
};

} // namespace west::rhi
