// =============================================================================
// WestEngine - Core
// Category/severity-based logging system with compile-time elimination
// =============================================================================
#pragma once

#include "core/Types.h"

#include <format>
#include <string_view>

namespace west
{

// ── Log Severity ───────────────────────────────────────────────────────────

enum class LogLevel : uint8
{
    Verbose, // Detailed diagnostic info (Debug only)
    Info,    // General information
    Warning, // Recoverable issues
    Error,   // Serious errors
    Fatal,   // Unrecoverable — triggers crash handler
};

// ── Log Category ───────────────────────────────────────────────────────────

enum class LogCategory : uint8
{
    Core,
    Platform,
    RHI,
    Render,
    Shader,
    Scene,
    Physics,
    Editor,
};

// ── Logger ─────────────────────────────────────────────────────────────────

class Logger
{
public:
    /// Initialize the logging system (call once at startup)
    static void Initialize();

    /// Shutdown the logging system (call once at exit)
    static void Shutdown();

    /// Core log function — prefer the WEST_LOG_* macros below
    static void Log(LogLevel level, LogCategory category, std::string_view message);

    /// Convenience: Fatal log (always active, used by WEST_ASSERT/WEST_CHECK)
    template <typename... Args>
    static void Fatal(std::format_string<Args...> fmt, Args&&... args)
    {
        Log(LogLevel::Fatal, LogCategory::Core, std::format(fmt, std::forward<Args>(args)...));
    }

private:
    static const char* LevelToString(LogLevel level);
    static const char* CategoryToString(LogCategory category);
};

} // namespace west

// ── Logging Macros ─────────────────────────────────────────────────────────
// Verbose and Info are compiled out in Release builds.

#if WEST_DEBUG
#define WEST_LOG_VERBOSE(category, fmt, ...)                                                                           \
    west::Logger::Log(west::LogLevel::Verbose, category, std::format(fmt __VA_OPT__(, ) __VA_ARGS__))

#define WEST_LOG_INFO(category, fmt, ...)                                                                              \
    west::Logger::Log(west::LogLevel::Info, category, std::format(fmt __VA_OPT__(, ) __VA_ARGS__))
#else
#define WEST_LOG_VERBOSE(category, fmt, ...) ((void)0)
#define WEST_LOG_INFO(category, fmt, ...) ((void)0)
#endif

#define WEST_LOG_WARNING(category, fmt, ...)                                                                           \
    west::Logger::Log(west::LogLevel::Warning, category, std::format(fmt __VA_OPT__(, ) __VA_ARGS__))

#define WEST_LOG_ERROR(category, fmt, ...)                                                                             \
    west::Logger::Log(west::LogLevel::Error, category, std::format(fmt __VA_OPT__(, ) __VA_ARGS__))

#define WEST_LOG_FATAL(category, fmt, ...)                                                                             \
    west::Logger::Log(west::LogLevel::Fatal, category, std::format(fmt __VA_OPT__(, ) __VA_ARGS__))
