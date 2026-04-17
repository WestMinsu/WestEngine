// =============================================================================
// WestEngine - RHI Interface
// Abstract fence (Timeline Semaphore) — CPU-GPU synchronization
// DX12: ID3D12Fence,  Vulkan: VkTimelineSemaphore
// =============================================================================
#pragma once

#include <cstdint>

namespace west::rhi
{

class IRHIFence
{
public:
    virtual ~IRHIFence() = default;

    /// Returns the value that the GPU has completed up to.
    virtual uint64_t GetCompletedValue() const = 0;

    /// Blocks the CPU until the fence reaches at least the given value.
    /// @param value     Target value to wait for.
    /// @param timeoutNs Timeout in nanoseconds. UINT64_MAX = infinite wait.
    virtual void Wait(uint64_t value, uint64_t timeoutNs = UINT64_MAX) = 0;

    /// Atomically advances the internal counter and returns the new value.
    /// Use this value as the signalValue when submitting to a queue.
    virtual uint64_t AdvanceValue() = 0;
};

} // namespace west::rhi
