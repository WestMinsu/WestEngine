// =============================================================================
// WestEngine - RHI Interface
// Abstract pipeline state object (Graphics / Compute)
// =============================================================================
#pragma once

#include <cstdint>

namespace west::rhi
{

enum class RHIPipelineType : uint8_t
{
    Graphics,
    Compute
};

class IRHIPipeline
{
public:
    virtual ~IRHIPipeline() = default;

    /// Pipeline bind point/type, used by command lists for root/layout selection.
    virtual RHIPipelineType GetType() const = 0;

    /// PSO cache hash (used by Phase 4 PSO cache system)
    virtual uint64_t GetPSOHash() const = 0;
};

} // namespace west::rhi
