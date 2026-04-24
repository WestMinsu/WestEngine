// =============================================================================
// WestEngine - Platform (Win32)
// Win32 window implementation
// =============================================================================
#pragma once

#include "platform/IWindow.h"

#include <string>

namespace west
{

class Win32Window final : public IWindow
{
public:
    Win32Window() = default;
    ~Win32Window() override;

    /// Create the Win32 window.
    /// @return true on success.
    [[nodiscard]] bool Create(const WindowDesc& desc);

    // ── IWindow interface ──────────────────────────────────────────────
    void PollEvents() override;
    [[nodiscard]] bool ShouldClose() const override
    {
        return m_shouldClose;
    }
    [[nodiscard]] void* GetNativeHandle() const override;
    [[nodiscard]] uint32 GetWidth() const override
    {
        return m_width;
    }
    [[nodiscard]] uint32 GetHeight() const override
    {
        return m_height;
    }
    void SetTitle(std::string_view title) override;

private:
    void* m_hwnd = nullptr; // HWND stored as void* to avoid header leak
    uint32 m_width = 0;
    uint32 m_height = 0;
    bool m_shouldClose = false;
    std::string m_title;

    std::wstring m_className;

    // Static window procedure — routes to instance
    static long long __stdcall WindowProc(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam);
};

} // namespace west
