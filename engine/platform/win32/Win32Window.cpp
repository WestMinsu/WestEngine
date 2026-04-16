// =============================================================================
// WestEngine - Platform (Win32)
// Win32 window implementation
// =============================================================================
#include "platform/win32/Win32Window.h"

#include "platform/win32/Win32Headers.h"
#include "core/Assert.h"
#include "core/Logger.h"

namespace west
{

Win32Window::~Win32Window()
{
    if (m_hwnd)
    {
        ::DestroyWindow(static_cast<HWND>(m_hwnd));
        m_hwnd = nullptr;
    }

    if (!m_className.empty())
    {
        ::UnregisterClassW(m_className.c_str(), ::GetModuleHandleW(nullptr));
    }
}

bool Win32Window::Create(const WindowDesc& desc)
{
    HINSTANCE hInstance = ::GetModuleHandleW(nullptr);

    // Register window class
    m_className = L"WestEngineWindowClass";

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = reinterpret_cast<WNDPROC>(&Win32Window::WindowProc);
    wc.hInstance     = hInstance;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = m_className.c_str();

    if (!::RegisterClassExW(&wc))
    {
        WEST_LOG_ERROR(LogCategory::Platform, "Failed to register window class");
        return false;
    }

    // Calculate window rect for desired client area
    RECT rect = { 0, 0, static_cast<LONG>(desc.width), static_cast<LONG>(desc.height) };
    ::AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

    int windowWidth  = rect.right - rect.left;
    int windowHeight = rect.bottom - rect.top;

    // Convert title to wide string
    std::wstring wideTitle(desc.title.begin(), desc.title.end());

    HWND hwnd = ::CreateWindowExW(
        0,
        m_className.c_str(),
        wideTitle.c_str(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        windowWidth, windowHeight,
        nullptr, nullptr,
        hInstance,
        this    // Pass this pointer for WM_NCCREATE
    );

    if (!hwnd)
    {
        WEST_LOG_ERROR(LogCategory::Platform, "Failed to create window");
        return false;
    }

    m_hwnd   = static_cast<void*>(hwnd);
    m_width  = desc.width;
    m_height = desc.height;

    ::ShowWindow(hwnd, SW_SHOW);
    ::UpdateWindow(hwnd);

    WEST_LOG_INFO(LogCategory::Platform,
        "Window created: {}x{} '{}'", desc.width, desc.height,
        std::string(desc.title));

    return true;
}

void Win32Window::PollEvents()
{
    MSG msg{};
    while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
        {
            m_shouldClose = true;
            return;
        }
        ::TranslateMessage(&msg);
        ::DispatchMessageW(&msg);
    }
}

void* Win32Window::GetNativeHandle() const
{
    return m_hwnd;
}

// ── Static Window Procedure ────────────────────────────────────────────────

long long __stdcall Win32Window::WindowProc(void* hwnd, unsigned int msg,
                                             unsigned long long wParam,
                                             long long lParam)
{
    HWND hWnd = static_cast<HWND>(hwnd);

    if (msg == WM_NCCREATE)
    {
        auto* createStruct = reinterpret_cast<CREATESTRUCTW*>(lParam);
        auto* window = static_cast<Win32Window*>(createStruct->lpCreateParams);
        ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    }

    auto* window = reinterpret_cast<Win32Window*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));

    switch (msg)
    {
        case WM_CLOSE:
            if (window)
            {
                window->m_shouldClose = true;
            }
            return 0;

        case WM_DESTROY:
            ::PostQuitMessage(0);
            return 0;

        case WM_SIZE:
            if (window)
            {
                window->m_width  = LOWORD(lParam);
                window->m_height = HIWORD(lParam);
            }
            return 0;
    }

    return ::DefWindowProcW(hWnd, msg, static_cast<WPARAM>(wParam),
                            static_cast<LPARAM>(lParam));
}

} // namespace west
