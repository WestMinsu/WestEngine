// =============================================================================
// WestEngine - Core
// Custom assertion macros for debug-time validation and fatal error reporting
// =============================================================================
#pragma once

#include "core/Types.h"
#include "core/Logger.h"
#include "platform/PlatformDetect.h"

// ── WEST_ASSERT ────────────────────────────────────────────────────────────
// Debug-only assertion. Triggers breakpoint on failure.
// Release builds: completely removed (zero cost).

#if WEST_DEBUG
    #define WEST_ASSERT(condition)                                              \
        do                                                                      \
        {                                                                       \
            if (!(condition))                                                   \
            {                                                                   \
                west::Logger::Fatal("ASSERT FAILED: {} ({}:{})",               \
                    #condition, __FILE__, __LINE__);                             \
                WEST_DEBUG_BREAK();                                              \
            }                                                                   \
        } while (0)
#else
    #define WEST_ASSERT(condition) ((void)0)
#endif

// ── WEST_CHECK ─────────────────────────────────────────────────────────────
// Always-active fatal check. Logs + dumps callstack + exits on failure.
// Active in both Debug and Release builds.

#define WEST_CHECK(condition, ...)                                              \
    do                                                                          \
    {                                                                           \
        if (!(condition))                                                       \
        {                                                                       \
            west::Logger::Fatal("CHECK FAILED: " __VA_ARGS__);                 \
            WEST_DEBUG_BREAK();                                                  \
        }                                                                       \
    } while (0)

// ── WEST_UNREACHABLE ───────────────────────────────────────────────────────
// Marks code paths that should never be reached.

#if WEST_DEBUG
    #define WEST_UNREACHABLE()                                                  \
        do                                                                      \
        {                                                                       \
            west::Logger::Fatal("UNREACHABLE code reached ({}:{})",            \
                __FILE__, __LINE__);                                             \
            WEST_DEBUG_BREAK();                                                  \
        } while (0)
#else
    #if defined(WEST_COMPILER_MSVC)
        #define WEST_UNREACHABLE() __assume(false)
    #else
        #define WEST_UNREACHABLE() __builtin_unreachable()
    #endif
#endif
