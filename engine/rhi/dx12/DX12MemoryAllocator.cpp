// =============================================================================
// WestEngine - RHI DX12
// D3D12MA memory allocator implementation
// =============================================================================
#include "rhi/dx12/DX12MemoryAllocator.h"

namespace west::rhi
{

DX12MemoryAllocator::~DX12MemoryAllocator()
{
    Shutdown();
}

bool DX12MemoryAllocator::Initialize(ID3D12Device* device, IDXGIAdapter* adapter)
{
    WEST_ASSERT(device != nullptr);
    WEST_ASSERT(adapter != nullptr);

    D3D12MA::ALLOCATOR_DESC allocatorDesc{};
    allocatorDesc.pDevice = device;
    allocatorDesc.pAdapter = adapter;
    // Allow D3D12MA to use the internal mutex for thread safety
    allocatorDesc.Flags = D3D12MA::ALLOCATOR_FLAG_NONE;

    HRESULT hr = D3D12MA::CreateAllocator(&allocatorDesc, &m_allocator);
    WEST_HR_CHECK(hr);

    // Query conservative CPU-visible GPU memory support via D3D12_FEATURE_DATA_ARCHITECTURE1.
    D3D12_FEATURE_DATA_ARCHITECTURE1 arch{};
    arch.NodeIndex = 0;
    if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &arch, sizeof(arch))))
    {
        // SharedSystemMemory is not a reliable ReBAR signal on discrete GPUs, so avoid over-reporting.
        m_supportsReBAR = arch.CacheCoherentUMA || arch.UMA;
    }

    WEST_LOG_INFO(LogCategory::RHI, "D3D12MA initialized. ReBAR: {}", m_supportsReBAR ? "Yes" : "No");
    return true;
}

void DX12MemoryAllocator::Shutdown()
{
    if (m_allocator)
    {
        LogStats();
        m_allocator->Release();
        m_allocator = nullptr;
        WEST_LOG_INFO(LogCategory::RHI, "D3D12MA shutdown.");
    }
}

void DX12MemoryAllocator::LogStats() const
{
    if (!m_allocator)
        return;

    D3D12MA::TotalStatistics stats{};
    m_allocator->CalculateStatistics(&stats);

    auto& total = stats.Total.Stats;
    WEST_LOG_INFO(LogCategory::RHI,
                  "D3D12MA Stats — Allocations: {}, Blocks: {}, Used: {} KB, Reserved: {} KB",
                  total.AllocationCount, total.BlockCount,
                  total.AllocationBytes / 1024, total.BlockBytes / 1024);

    if (total.AllocationCount > 0)
    {
        WEST_LOG_WARNING(LogCategory::RHI,
                         "D3D12MA: {} allocations still alive at shutdown!", total.AllocationCount);
    }
}

} // namespace west::rhi
