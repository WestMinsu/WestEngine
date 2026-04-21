// =============================================================================
// WestEngine - Platform (Win32)
// Optional PIX programmatic GPU capture helper
// =============================================================================
#pragma once

#include <string>

namespace west
{

class PixProgrammaticCapture final
{
public:
    void Initialize();

    [[nodiscard]] bool IsAvailable() const
    {
        return m_beginCapture != nullptr && m_endCapture != nullptr;
    }

    [[nodiscard]] bool BeginGpuCapture(const std::wstring& capturePath);
    [[nodiscard]] bool EndCapture();

private:
    void* m_runtimeModule = nullptr;

    using BeginCaptureFn = long(__stdcall*)(unsigned long captureFlags, const void* captureParameters);
    using EndCaptureFn = long(__stdcall*)(int discard);

    BeginCaptureFn m_beginCapture = nullptr;
    EndCaptureFn m_endCapture = nullptr;
};

} // namespace west
