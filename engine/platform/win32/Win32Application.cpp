// =============================================================================
// WestEngine - Platform (Win32)
// Win32 application lifecycle implementation
// =============================================================================
#include "platform/win32/Win32Application.h"

#include "core/Assert.h"
#include "core/Logger.h"
#include "core/Profiler.h"

namespace west
{

bool Win32Application::Initialize()
{
    WEST_PROFILE_FUNCTION();
    Logger::Initialize();

    WEST_LOG_INFO(LogCategory::Core, "WestEngine v0.1.0 initializing...");

    // Create main window
    m_window = std::make_unique<Win32Window>();

    WindowDesc windowDesc;
    windowDesc.title = "WestEngine";
    windowDesc.width = 1920;
    windowDesc.height = 1080;

    if (!m_window->Create(windowDesc))
    {
        WEST_LOG_FATAL(LogCategory::Platform, "Failed to create main window");
        return false;
    }

    m_timer.Reset();
    m_isRunning = true;

    WEST_LOG_INFO(LogCategory::Core, "Initialization complete.");
    return true;
}

void Win32Application::Run()
{
    WEST_LOG_INFO(LogCategory::Core, "Entering main loop...");

    while (m_isRunning)
    {
        m_timer.Tick();
        m_window->PollEvents();

        if (m_window->ShouldClose())
        {
            m_isRunning = false;
            break;
        }

        // TODO(minsu): Phase 1 — RHI 초기화 후 여기서 렌더링 루프 실행
        // 현재는 빈 프레임 (윈도우 생성 마일스톤)

        WEST_FRAME_MARK;
    }

    WEST_LOG_INFO(LogCategory::Core, "Main loop exited.");
}

void Win32Application::Shutdown()
{
    WEST_PROFILE_FUNCTION();
    WEST_LOG_INFO(LogCategory::Core, "Shutting down...");

    m_window.reset();

    Logger::Shutdown();
}

// ── Application Factory ──────────────────────────────────────────────────
std::unique_ptr<IApplication> CreateApplication()
{
    return std::make_unique<Win32Application>();
}

} // namespace west
