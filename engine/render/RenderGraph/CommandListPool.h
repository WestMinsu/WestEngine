// =============================================================================
// WestEngine - Render
// Lightweight command list pool for render-graph execution
// =============================================================================
#pragma once

#include "rhi/interface/RHIEnums.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace west::rhi
{
class IRHICommandList;
class IRHIDevice;
} // namespace west::rhi

namespace west::render
{

class CommandListPool
{
public:
    struct Lease
    {
        rhi::IRHICommandList* commandList = nullptr;
        uint32_t entryIndex = UINT32_MAX;

        [[nodiscard]] bool IsValid() const
        {
            return commandList != nullptr && entryIndex != UINT32_MAX;
        }
    };

    [[nodiscard]] Lease Acquire(rhi::IRHIDevice& device, rhi::RHIQueueType queueType, uint64_t completedFenceValue);
    void Release(Lease lease, uint64_t availableAfterFenceValue);
    void Reset();

private:
    struct Entry
    {
        std::unique_ptr<rhi::IRHICommandList> commandList;
        rhi::RHIQueueType queueType = rhi::RHIQueueType::Graphics;
        uint64_t availableAfterFenceValue = 0;
        bool inUse = false;
    };

    std::vector<Entry> m_entries;
};

} // namespace west::render
