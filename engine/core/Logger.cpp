// =============================================================================
// WestEngine - Core
// Logger implementation
// =============================================================================
#include "core/Logger.h"

#include <chrono>
#include <cstdio>
#include <mutex>

namespace west
{

// ── Static state ───────────────────────────────────────────────────────────

static std::mutex s_logMutex;

// ── Initialize / Shutdown ──────────────────────────────────────────────────

void Logger::Initialize()
{
    Log(LogLevel::Info, LogCategory::Core, "Logger initialized.");
}

void Logger::Shutdown()
{
    Log(LogLevel::Info, LogCategory::Core, "Logger shutting down.");
}

// ── Core Log Function ──────────────────────────────────────────────────────

void Logger::Log(LogLevel level, LogCategory category, std::string_view message)
{
    // Thread-safe console output (initialization/teardown only; will be
    // replaced with lock-free ring buffer in production)
    std::lock_guard lock(s_logMutex);

    // Timestamp (ms since epoch, lightweight)
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();

    // Format: [TIME] [LEVEL] [CATEGORY] Message
    std::fprintf(stderr, "[%lld] [%-7s] [%-8s] %.*s\n", static_cast<long long>(ms), LevelToString(level),
                 CategoryToString(category), static_cast<int>(message.size()), message.data());

    // Flush immediately for all levels since we run in capturing environments
    std::fflush(stderr);
}

// ── String Conversion ──────────────────────────────────────────────────────

const char* Logger::LevelToString(LogLevel level)
{
    switch (level)
    {
    case LogLevel::Verbose:
        return "VERBOSE";
    case LogLevel::Info:
        return "INFO";
    case LogLevel::Warning:
        return "WARNING";
    case LogLevel::Error:
        return "ERROR";
    case LogLevel::Fatal:
        return "FATAL";
    }
    return "UNKNOWN";
}

const char* Logger::CategoryToString(LogCategory category)
{
    switch (category)
    {
    case LogCategory::Core:
        return "Core";
    case LogCategory::Platform:
        return "Platform";
    case LogCategory::RHI:
        return "RHI";
    case LogCategory::Render:
        return "Render";
    case LogCategory::Shader:
        return "Shader";
    case LogCategory::Scene:
        return "Scene";
    case LogCategory::Physics:
        return "Physics";
    case LogCategory::Editor:
        return "Editor";
    }
    return "Unknown";
}

} // namespace west
