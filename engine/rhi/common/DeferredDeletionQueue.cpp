// =============================================================================
// WestEngine - RHI Common
// Deferred Deletion Queue
// =============================================================================
#include "rhi/common/DeferredDeletionQueue.h"

#include <algorithm>
#include <iterator>

namespace west::rhi
{

DeferredDeletionQueue::~DeferredDeletionQueue()
{
    // Ensure everything is destroyed when queue is deleted (device shutdown)
    FlushAll();
}

void DeferredDeletionQueue::Enqueue(std::function<void()> deleter, uint64_t fenceValue)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_queue.push_back({fenceValue, std::move(deleter)});
}

void DeferredDeletionQueue::Flush(uint64_t completedFenceValue)
{
    std::vector<Entry> readyEntries;

    {
        std::lock_guard<std::mutex> lock(m_mutex);

        auto it = std::stable_partition(m_queue.begin(), m_queue.end(),
                                        [completedFenceValue](const Entry& entry)
                                        {
                                            return entry.fenceValue > completedFenceValue;
                                        });

        // The range [it, m_queue.end()) contains entries that should be deleted.
        readyEntries.assign(std::make_move_iterator(it), std::make_move_iterator(m_queue.end()));
        m_queue.erase(it, m_queue.end());
    }

    for (Entry& entry : readyEntries)
    {
        if (entry.deleter)
        {
            entry.deleter();
        }
    }
}

void DeferredDeletionQueue::FlushAll()
{
    for (;;)
    {
        std::vector<Entry> entries;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_queue.empty())
            {
                return;
            }

            entries = std::move(m_queue);
            m_queue.clear();
        }

        for (Entry& entry : entries)
        {
            if (entry.deleter)
            {
                entry.deleter();
            }
        }
    }
}

} // namespace west::rhi
