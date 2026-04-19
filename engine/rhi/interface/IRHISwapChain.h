// =============================================================================
// WestEngine - RHI Interface
// Abstract swap chain — presentation and back buffer management
// =============================================================================
#pragma once

#include "rhi/interface/RHIEnums.h"

#include <cstdint>

namespace west::rhi
{

class IRHITexture;
class IRHISemaphore;

class IRHISwapChain
{
public:
    virtual ~IRHISwapChain() = default;

    /// Acquire the next back buffer index for rendering.
    /// @param acquireSemaphore  Vulkan: signal on acquire / DX12: ignored
    virtual uint32_t AcquireNextImage(IRHISemaphore* acquireSemaphore = nullptr) = 0;

    /// Present the current frame to the display.
    /// @return true when presented successfully, false when the swap chain should be resized.
    /// @param presentSemaphore  Vulkan: wait before present / DX12: ignored
    virtual bool Present(IRHISemaphore* presentSemaphore = nullptr) = 0;

    /// Get the current back buffer as an IRHITexture.
    virtual IRHITexture* GetCurrentBackBuffer() = 0;

    /// Resize the swap chain (on window resize).
    virtual void Resize(uint32_t width, uint32_t height) = 0;

    virtual uint32_t GetCurrentIndex() const = 0;
    virtual uint32_t GetBufferCount() const = 0;
    virtual RHIFormat GetFormat() const = 0;
};

} // namespace west::rhi
