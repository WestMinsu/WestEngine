// =============================================================================
// WestEngine - RHI Interface
// Abstract pipeline state object (Graphics / Compute)
// =============================================================================
#pragma once

#include <cstdint>

namespace west::rhi
{

class IRHIPipeline
{
public:
    virtual ~IRHIPipeline() = default;

    /// PSO cache hash (used by Phase 4 PSO cache system)
    virtual uint64_t GetPSOHash() const = 0;
};

} // namespace west::rhi
