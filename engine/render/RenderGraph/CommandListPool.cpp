// =============================================================================
// WestEngine - Render
// Lightweight command list pool for render-graph execution
// =============================================================================
#include "render/RenderGraph/CommandListPool.h"

#include "core/Assert.h"
#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/IRHIDevice.h"

namespace west::render
{

CommandListPool::Lease CommandListPool::Acquire(rhi::IRHIDevice& device, rhi::RHIQueueType queueType,
                                                uint64_t completedFenceValue)
{
    for (uint32_t entryIndex = 0; entryIndex < m_entries.size(); ++entryIndex)
    {
        Entry& entry = m_entries[entryIndex];
        if (entry.inUse || entry.queueType != queueType || entry.availableAfterFenceValue > completedFenceValue)
        {
            continue;
        }

        entry.inUse = true;
        return Lease{.commandList = entry.commandList.get(), .entryIndex = entryIndex};
    }

    Entry entry{};
    entry.queueType = queueType;
    entry.commandList = device.CreateCommandList(queueType);
    WEST_ASSERT(entry.commandList != nullptr);
    entry.inUse = true;

    const uint32_t entryIndex = static_cast<uint32_t>(m_entries.size());
    m_entries.push_back(std::move(entry));
    return Lease{.commandList = m_entries.back().commandList.get(), .entryIndex = entryIndex};
}

void CommandListPool::Release(Lease lease, uint64_t availableAfterFenceValue)
{
    WEST_ASSERT(lease.IsValid());
    WEST_ASSERT(lease.entryIndex < m_entries.size());

    Entry& entry = m_entries[lease.entryIndex];
    WEST_ASSERT(entry.commandList.get() == lease.commandList);
    WEST_ASSERT(entry.inUse);

    entry.inUse = false;
    entry.availableAfterFenceValue = availableAfterFenceValue;
}

void CommandListPool::Reset()
{
    m_entries.clear();
}

} // namespace west::render
