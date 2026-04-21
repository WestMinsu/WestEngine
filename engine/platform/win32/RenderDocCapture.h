// =============================================================================
// WestEngine - Platform (Win32)
// Optional RenderDoc in-application capture helper
// =============================================================================
#pragma once

#include <cstdint>

namespace west
{

class RenderDocCapture final
{
public:
    void Initialize();

    [[nodiscard]] bool IsAvailable() const
    {
        return m_api != nullptr;
    }

    void BeginFrameCapture(const char* captureTitle = nullptr);
    [[nodiscard]] bool EndFrameCapture();

private:
    struct API;
    API* m_api = nullptr;
};

} // namespace west
