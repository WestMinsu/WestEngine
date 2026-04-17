// =============================================================================
// WestEngine - Core
// Tracy Profiler integration wrapper
// Provides zero-cost macros when TRACY_ENABLE is not defined (Release builds).
// =============================================================================
#pragma once

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#endif

// ── Frame Mark ─────────────────────────────────────────────────────────────
// Call once per frame at the end of the frame loop.

#ifdef TRACY_ENABLE
#define WEST_FRAME_MARK FrameMark
#else
#define WEST_FRAME_MARK ((void)0)
#endif

// ── Scoped Zone ────────────────────────────────────────────────────────────
// Marks a scope (function, block) for profiling.
// Usage: WEST_PROFILE_SCOPE("MyFunction");

#ifdef TRACY_ENABLE
#define WEST_PROFILE_SCOPE(name) ZoneScopedN(name)
#define WEST_PROFILE_FUNCTION() ZoneScoped
#else
#define WEST_PROFILE_SCOPE(name) ((void)0)
#define WEST_PROFILE_FUNCTION() ((void)0)
#endif

// ── Named Plots ────────────────────────────────────────────────────────────
// Track numeric values over time (e.g., FPS, memory usage).
// Usage: WEST_PROFILE_PLOT("FPS", fps);

#ifdef TRACY_ENABLE
#define WEST_PROFILE_PLOT(name, value) TracyPlot(name, value)
#else
#define WEST_PROFILE_PLOT(name, value) ((void)0)
#endif

// ── Memory Tracking ────────────────────────────────────────────────────────
// Track allocations and frees. Useful for visualizing allocator activity.
// Usage: WEST_PROFILE_ALLOC(ptr, size);  WEST_PROFILE_FREE(ptr);

#ifdef TRACY_ENABLE
#define WEST_PROFILE_ALLOC(ptr, size) TracyAlloc(ptr, size)
#define WEST_PROFILE_FREE(ptr) TracyFree(ptr)
#else
#define WEST_PROFILE_ALLOC(ptr, size) ((void)0)
#define WEST_PROFILE_FREE(ptr) ((void)0)
#endif
