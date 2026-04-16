// =============================================================================
// WestEngine - Core
// Timer implementation
// =============================================================================
#include "core/Timer.h"

namespace west
{

Timer::Timer()
{
    Reset();
}

void Timer::Reset()
{
    m_startTime    = Clock::now();
    m_lastTickTime = m_startTime;
    m_deltaTime    = 0.0;
    m_totalTime    = 0.0;
}

void Timer::Tick()
{
    auto now = Clock::now();

    std::chrono::duration<float64> delta = now - m_lastTickTime;
    std::chrono::duration<float64> total = now - m_startTime;

    m_deltaTime    = delta.count();
    m_totalTime    = total.count();
    m_lastTickTime = now;
}

float64 Timer::GetFPS() const
{
    if (m_deltaTime > 0.0)
    {
        return 1.0 / m_deltaTime;
    }
    return 0.0;
}

} // namespace west
