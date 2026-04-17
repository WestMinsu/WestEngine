// =============================================================================
// WestEngine - RHI Interface
// Binary semaphore — Swapchain Acquire/Present synchronization only
// DX12: internally emulated via Fence,  Vulkan: VkSemaphore (binary)
// =============================================================================
#pragma once

namespace west::rhi
{

class IRHISemaphore
{
public:
    virtual ~IRHISemaphore() = default;
    // No external API — used internally by Submit/Present
};

} // namespace west::rhi
