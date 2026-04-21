// =============================================================================
// WestEngine - RHI Common
// Bindless index pool shared by graphics backends
// =============================================================================
#pragma once

#include "core/Types.h"
#include "rhi/interface/RHIEnums.h"

#include <vector>

namespace west::rhi
{

class BindlessPool final
{
public:
    BindlessPool() = default;
    explicit BindlessPool(uint32 capacity);

    void Initialize(uint32 capacity);

    [[nodiscard]] BindlessIndex Allocate();
    [[nodiscard]] bool Free(BindlessIndex index);
    [[nodiscard]] bool IsAllocated(BindlessIndex index) const;

    [[nodiscard]] uint32 GetCapacity() const;
    [[nodiscard]] uint32 GetAllocatedCount() const;

private:
    std::vector<BindlessIndex> m_freeList;
    std::vector<uint8> m_allocated;
    uint32 m_allocatedCount = 0;
};

} // namespace west::rhi
