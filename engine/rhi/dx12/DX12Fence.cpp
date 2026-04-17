// =============================================================================
// WestEngine - RHI DX12
// DX12 fence implementation — SetEventOnCompletion pattern
// Reference: D3D12Lecture-main/11_OverlappedFrames
// =============================================================================
#include "rhi/dx12/DX12Fence.h"

#include "platform/win32/Win32Headers.h"

namespace west::rhi
{

DX12Fence::~DX12Fence()
{
    if (m_fenceEvent)
    {
        ::CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
}

void DX12Fence::Initialize(ID3D12Device* device, uint64_t initialValue)
{
    m_nextValue.store(initialValue, std::memory_order_relaxed);

    WEST_HR_CHECK(device->CreateFence(initialValue, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));

    // Create Win32 auto-reset event for blocking wait
    m_fenceEvent = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
    WEST_CHECK(m_fenceEvent != nullptr, "Failed to create fence event");

    WEST_LOG_VERBOSE(LogCategory::RHI, "DX12 Fence created (initial value: {})", initialValue);
}

uint64_t DX12Fence::GetCompletedValue() const
{
    return m_fence->GetCompletedValue();
}

void DX12Fence::Wait(uint64_t value, uint64_t timeoutNs)
{
    if (m_fence->GetCompletedValue() >= value)
    {
        return; // Already completed — no wait needed
    }

    WEST_HR_CHECK(m_fence->SetEventOnCompletion(value, m_fenceEvent));

    // Convert nanoseconds to milliseconds for Win32
    DWORD timeoutMs = INFINITE;
    if (timeoutNs != UINT64_MAX)
    {
        timeoutMs = static_cast<DWORD>(timeoutNs / 1'000'000);
        if (timeoutMs == 0 && timeoutNs > 0)
        {
            timeoutMs = 1; // Minimum 1ms
        }
    }

    DWORD result = ::WaitForSingleObject(m_fenceEvent, timeoutMs);
    if (result == WAIT_TIMEOUT)
    {
        WEST_LOG_WARNING(LogCategory::RHI, "DX12 Fence wait timed out (target: {}, completed: {})", value,
                         m_fence->GetCompletedValue());
    }
}

uint64_t DX12Fence::AdvanceValue()
{
    return m_nextValue.fetch_add(1, std::memory_order_relaxed) + 1;
}

} // namespace west::rhi
