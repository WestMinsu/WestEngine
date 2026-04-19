// =============================================================================
// WestEngine - RHI Common
// Deferred Deletion Queue - Safely defers GPU resource destruction until fence completes
// =============================================================================
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace west::rhi
{

class DeferredDeletionQueue
{
public:
    DeferredDeletionQueue() = default;
    ~DeferredDeletionQueue();

    /// Enqueues a lambda that will physically destroy a GPU resource.
    /// The lambda will be executed when the GPU fence reaches or exceeds 'fenceValue'.
    void Enqueue(std::function<void()> deleter, uint64_t fenceValue);

    /// Executes all deleters whose fence value <= targetFenceValue.
    void Flush(uint64_t completedFenceValue);

    /// Forces destruction of all remaining objects, ignoring fence values.
    /// Used only during application shutdown or device loss.
    void FlushAll();

private:
    struct Entry
    {
        uint64_t fenceValue;
        std::function<void()> deleter;
    };

    std::vector<Entry> m_queue;
    std::mutex m_mutex;
};

} // namespace west::rhi
