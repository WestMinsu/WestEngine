// =============================================================================
// WestEngine - RHI Common
// Deferred Deletion Queue
// =============================================================================
#include "rhi/common/DeferredDeletionQueue.h"

#include <algorithm>

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
    std::lock_guard<std::mutex> lock(m_mutex);

    // Iterating backwards handles removal cleanly, but for deletion order we can 
    // separate items to delete from ones to keep.
    auto it = std::partition(m_queue.begin(), m_queue.end(),
                             [completedFenceValue](const Entry& entry) {
                                 // True if we should keep it (fence not yet reached)
                                 return entry.fenceValue > completedFenceValue;
                             });

    // The range [it, m_queue.end()) contains entries that should be deleted
    for (auto iter = it; iter != m_queue.end(); ++iter)
    {
        if (iter->deleter)
        {
            iter->deleter();
        }
    }

    // Erase the executed ones
    m_queue.erase(it, m_queue.end());
}

void DeferredDeletionQueue::FlushAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for (auto& entry : m_queue)
    {
        if (entry.deleter)
        {
            entry.deleter();
        }
    }
    m_queue.clear();
}

} // namespace west::rhi
