// =============================================================================
// WestEngine - Core Threading
// Minimal worker-thread task system for parallel command recording
// =============================================================================
#pragma once

#include "core/Types.h"

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace west
{

class TaskSystem final
{
public:
    TaskSystem() = default;
    ~TaskSystem();

    TaskSystem(const TaskSystem&) = delete;
    TaskSystem& operator=(const TaskSystem&) = delete;

    void Initialize(uint32 workerCount = 0);
    void Shutdown();

    void Dispatch(uint32 taskCount, const std::function<void(uint32 taskIndex)>& task);
    void Wait();

    [[nodiscard]] uint32 GetWorkerCount() const
    {
        return static_cast<uint32>(m_workers.size());
    }

private:
    void WorkerLoop();

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()>> m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_workAvailable;
    std::condition_variable m_idle;
    std::atomic<uint32> m_pendingTasks = 0;
    bool m_stop = false;
};

} // namespace west
