// =============================================================================
// WestEngine - Platform (Win32)
// Win32 application lifecycle
// =============================================================================
#pragma once

#include "platform/IApplication.h"
#include "platform/win32/Win32Window.h"
#include "core/Timer.h"

#include <memory>

namespace west
{

class Win32Application final : public IApplication
{
public:
    Win32Application() = default;
    ~Win32Application() override = default;

    // ── IApplication interface ─────────────────────────────────────────
    [[nodiscard]] bool Initialize() override;
    void Run() override;
    void Shutdown() override;

    /// Access the main window.
    [[nodiscard]] IWindow* GetWindow() const { return m_window.get(); }

private:
    std::unique_ptr<Win32Window> m_window;
    Timer m_timer;
    bool  m_isRunning = false;
};

} // namespace west
