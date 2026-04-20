// =============================================================================
// WestEngine - Core Threading
// Minimal task system implementation
// =============================================================================
#include "core/Threading/TaskSystem.h"

#include "core/Assert.h"

#include <algorithm>

namespace west
{

TaskSystem::~TaskSystem()
{
    Shutdown();
}

void TaskSystem::Initialize(uint32 workerCount)
{
    if (!m_workers.empty())
    {
        return;
    }

    if (workerCount == 0)
    {
        const uint32 hardwareThreads = std::max<uint32>(1, std::thread::hardware_concurrency());
        workerCount = std::max<uint32>(1, hardwareThreads > 1 ? hardwareThreads - 1 : 1);
    }

    m_stop = false;
    m_workers.reserve(workerCount);
    for (uint32 i = 0; i < workerCount; ++i)
    {
        m_workers.emplace_back([this]() {
            WorkerLoop();
        });
    }
}

void TaskSystem::Shutdown()
{
    {
        std::lock_guard lock(m_mutex);
        m_stop = true;
    }

    m_workAvailable.notify_all();

    for (std::thread& worker : m_workers)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }

    m_workers.clear();
}

void TaskSystem::Dispatch(uint32 taskCount, const std::function<void(uint32 taskIndex)>& task)
{
    WEST_ASSERT(taskCount > 0);
    WEST_ASSERT(task != nullptr);

    {
        std::lock_guard lock(m_mutex);
        m_pendingTasks += taskCount;

        for (uint32 taskIndex = 0; taskIndex < taskCount; ++taskIndex)
        {
            m_tasks.push([task, taskIndex]() {
                task(taskIndex);
            });
        }
    }

    m_workAvailable.notify_all();
}

void TaskSystem::Wait()
{
    std::unique_lock lock(m_mutex);
    m_idle.wait(lock, [this]() {
        return m_pendingTasks.load() == 0 && m_tasks.empty();
    });
}

void TaskSystem::WorkerLoop()
{
    while (true)
    {
        std::function<void()> task;

        {
            std::unique_lock lock(m_mutex);
            m_workAvailable.wait(lock, [this]() {
                return m_stop || !m_tasks.empty();
            });

            if (m_stop && m_tasks.empty())
            {
                return;
            }

            task = std::move(m_tasks.front());
            m_tasks.pop();
        }

        task();

        if (--m_pendingTasks == 0)
        {
            std::lock_guard lock(m_mutex);
            m_idle.notify_all();
        }
    }
}

} // namespace west
