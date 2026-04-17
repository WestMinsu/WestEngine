// =============================================================================
// WestEngine - Core
// High-precision timer using std::chrono
// =============================================================================
#pragma once

#include "core/Types.h"

#include <chrono>

namespace west
{

/// High-resolution timer for frame timing and elapsed time measurement.
class Timer
{
public:
    Timer();

    /// Reset the timer to zero.
    void Reset();

    /// Call once per frame to update delta time.
    void Tick();

    /// Time elapsed since last Tick() call, in seconds.
    [[nodiscard]] float64 GetDeltaTime() const
    {
        return m_deltaTime;
    }

    /// Total time since last Reset(), in seconds.
    [[nodiscard]] float64 GetTotalTime() const
    {
        return m_totalTime;
    }

    /// Frames per second based on current delta time.
    [[nodiscard]] float64 GetFPS() const;

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    TimePoint m_startTime;
    TimePoint m_lastTickTime;
    float64 m_deltaTime = 0.0;
    float64 m_totalTime = 0.0;
};

} // namespace west
