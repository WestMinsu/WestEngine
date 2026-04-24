// =============================================================================
// WestEngine - Platform
// Window interface — OS-independent window abstraction
// =============================================================================
#pragma once

#include "core/Types.h"

#include <string_view>

namespace west
{

/// Window creation descriptor
struct WindowDesc
{
    std::string_view title = "WestEngine";
    uint32 width = 1920;
    uint32 height = 1080;
};

/// Abstract window interface.
/// Implementations: Win32Window (win32/), future: X11Window, CocoaWindow.
class IWindow
{
public:
    virtual ~IWindow() = default;

    /// Process OS messages. Call once per frame.
    virtual void PollEvents() = 0;

    /// Returns true when the window should close (user clicked X, etc.)
    [[nodiscard]] virtual bool ShouldClose() const = 0;

    /// Returns the native window handle as void* (HWND on Win32).
    /// Used by RHI SwapChain creation — avoids leaking platform types.
    [[nodiscard]] virtual void* GetNativeHandle() const = 0;

    /// Current client area dimensions.
    [[nodiscard]] virtual uint32 GetWidth() const = 0;
    [[nodiscard]] virtual uint32 GetHeight() const = 0;

    /// Update the OS window title.
    virtual void SetTitle(std::string_view title) = 0;
};

} // namespace west
