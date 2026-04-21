// =============================================================================
// WestEngine - Platform (Win32)
// Optional PIX GPU capturer loader for DX12 capture sessions
// =============================================================================
#pragma once

namespace west
{

class PixGpuCapturerLoader final
{
public:
    void Initialize();

    [[nodiscard]] bool IsLoaded() const
    {
        return m_module != nullptr;
    }

private:
    void* m_module = nullptr;
};

} // namespace west
