// =============================================================================
// WestEngine - Core
// Platform detection macros for compile-time platform identification
// =============================================================================
#pragma once

// ── Platform Detection ─────────────────────────────────────────────────────

#if defined(_WIN32) || defined(_WIN64)
    #define WEST_PLATFORM_WINDOWS 1
#elif defined(__linux__)
    #define WEST_PLATFORM_LINUX 1
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC
        #define WEST_PLATFORM_MACOS 1
    #endif
#else
    #error "Unsupported platform"
#endif

// ── Compiler Detection ─────────────────────────────────────────────────────

#if defined(_MSC_VER)
    #define WEST_COMPILER_MSVC 1
    #define WEST_DEBUG_BREAK() __debugbreak()
#elif defined(__clang__)
    #define WEST_COMPILER_CLANG 1
    #define WEST_DEBUG_BREAK() __builtin_debugtrap()
#elif defined(__GNUC__)
    #define WEST_COMPILER_GCC 1
    #define WEST_DEBUG_BREAK() __builtin_trap()
#else
    #define WEST_DEBUG_BREAK() ((void)0)
#endif

// ── Architecture Detection ─────────────────────────────────────────────────

#if defined(_M_X64) || defined(__x86_64__)
    #define WEST_ARCH_X64 1
#elif defined(_M_ARM64) || defined(__aarch64__)
    #define WEST_ARCH_ARM64 1
#endif
