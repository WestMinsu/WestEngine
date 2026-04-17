// =============================================================================
// WestEngine - RHI DX12
// DX12 binary semaphore — no-op on DX12 (Fence handles everything)
// =============================================================================
#pragma once

#include "rhi/interface/IRHISemaphore.h"

namespace west::rhi
{

/// DX12 does not require binary semaphores for swapchain synchronization.
/// DXGI handles Acquire/Present synchronization internally.
/// This class exists solely to satisfy the IRHISemaphore interface contract.
class DX12Semaphore final : public IRHISemaphore
{
public:
    DX12Semaphore() = default;
    ~DX12Semaphore() override = default;
};

} // namespace west::rhi
