// =============================================================================
// WestEngine - RHI DX12
// D3D12MA memory allocator wrapper — Placed Resource allocation
// =============================================================================
#pragma once

#include "rhi/dx12/DX12Helpers.h"

// D3D12 Memory Allocator — DX12Helpers.h already includes d3d12.h from Windows SDK,
// so tell D3D12MA to skip its own #include <d3d12.h> / <dxguids/dxguids.h>.
#define D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED
#include <D3D12MemAlloc.h>

namespace west::rhi
{

class DX12MemoryAllocator
{
public:
    DX12MemoryAllocator() = default;
    ~DX12MemoryAllocator();

    // Non-copyable, non-movable — singleton per device
    DX12MemoryAllocator(const DX12MemoryAllocator&) = delete;
    DX12MemoryAllocator& operator=(const DX12MemoryAllocator&) = delete;

    /// Initialize D3D12MA allocator.
    [[nodiscard]] bool Initialize(ID3D12Device* device, IDXGIAdapter* adapter);

    /// Shutdown and release the allocator.
    void Shutdown();

    /// Get the underlying D3D12MA allocator.
    D3D12MA::Allocator* GetAllocator() const { return m_allocator; }

    /// Whether the GPU supports Resizable BAR (large host-visible VRAM).
    bool SupportsReBAR() const { return m_supportsReBAR; }

    /// Log allocation statistics (call at shutdown to verify zero leaks).
    void LogStats() const;

private:
    D3D12MA::Allocator* m_allocator = nullptr;
    bool m_supportsReBAR = false;
};

} // namespace west::rhi
