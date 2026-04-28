// =============================================================================
// WestEngine - Platform (Win32)
// Win32 window implementation
// =============================================================================
#include "platform/win32/Win32Window.h"

#include "core/Assert.h"
#include "core/Logger.h"
#include "platform/win32/Win32Headers.h"

namespace west
{

namespace
{

[[nodiscard]] bool GetWindowRectForClientSize(uint32 clientWidth, uint32 clientHeight, DWORD style, DWORD exStyle,
                                              RECT& outRect)
{
    outRect = {0, 0, static_cast<LONG>(clientWidth), static_cast<LONG>(clientHeight)};
    return ::AdjustWindowRectEx(&outRect, style, FALSE, exStyle) != 0;
}

[[nodiscard]] uint32 RectWidth(const RECT& rect)
{
    return static_cast<uint32>(rect.right - rect.left);
}

[[nodiscard]] uint32 RectHeight(const RECT& rect)
{
    return static_cast<uint32>(rect.bottom - rect.top);
}

} // namespace

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
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = reinterpret_cast<WNDPROC>(&Win32Window::WindowProc);
    wc.hInstance = hInstance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
    wc.lpszClassName = m_className.c_str();

    if (!::RegisterClassExW(&wc))
    {
        WEST_LOG_ERROR(LogCategory::Platform, "Failed to register window class");
        return false;
    }

    constexpr DWORD kWindowStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
    constexpr DWORD kWindowExStyle = WS_EX_APPWINDOW;

    // Borderless output keeps the renderable client area at the requested capture resolution.
    RECT rect{};
    if (!GetWindowRectForClientSize(desc.width, desc.height, kWindowStyle, kWindowExStyle, rect))
    {
        WEST_LOG_ERROR(LogCategory::Platform, "Failed to calculate Win32 window rect");
        return false;
    }

    const int windowWidth = static_cast<int>(RectWidth(rect));
    const int windowHeight = static_cast<int>(RectHeight(rect));

    const int screenWidth = ::GetSystemMetrics(SM_CXSCREEN);
    const int screenHeight = ::GetSystemMetrics(SM_CYSCREEN);
    const int windowX = (screenWidth - windowWidth) / 2;
    const int windowY = (screenHeight - windowHeight) / 2;

    m_title = std::string(desc.title);
    std::wstring wideTitle(m_title.begin(), m_title.end());

    HWND hwnd = ::CreateWindowExW(kWindowExStyle, m_className.c_str(), wideTitle.c_str(), kWindowStyle, windowX,
                                  windowY, windowWidth, windowHeight, nullptr, nullptr, hInstance,
                                  this // Pass this pointer for WM_NCCREATE
    );

    if (!hwnd)
    {
        WEST_LOG_ERROR(LogCategory::Platform, "Failed to create window");
        return false;
    }

    m_hwnd = static_cast<void*>(hwnd);
    ::ShowWindow(hwnd, SW_SHOW);
    ::UpdateWindow(hwnd);

    RECT clientRect{};
    if (::GetClientRect(hwnd, &clientRect) == 0)
    {
        WEST_LOG_ERROR(LogCategory::Platform, "Failed to query Win32 client rect");
        return false;
    }

    m_width = RectWidth(clientRect);
    m_height = RectHeight(clientRect);

    if (m_width != desc.width || m_height != desc.height)
    {
        WEST_LOG_WARNING(LogCategory::Platform, "Window client size requested {}x{}, actual {}x{}.", desc.width,
                         desc.height, m_width, m_height);
    }

    WEST_LOG_INFO(LogCategory::Platform, "Window created: client {}x{} '{}'", m_width, m_height,
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

void Win32Window::SetTitle(std::string_view title)
{
    m_title = std::string(title);
    if (m_hwnd == nullptr)
    {
        return;
    }

    std::wstring wideTitle(m_title.begin(), m_title.end());
    ::SetWindowTextW(static_cast<HWND>(m_hwnd), wideTitle.c_str());
}

// ── Static Window Procedure ────────────────────────────────────────────────

long long __stdcall Win32Window::WindowProc(void* hwnd, unsigned int msg, unsigned long long wParam, long long lParam)
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
            window->m_width = LOWORD(lParam);
            window->m_height = HIWORD(lParam);
        }
        return 0;
    }

    return ::DefWindowProcW(hWnd, msg, static_cast<WPARAM>(wParam), static_cast<LPARAM>(lParam));
}

} // namespace west
