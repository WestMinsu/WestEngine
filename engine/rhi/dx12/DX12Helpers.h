// =============================================================================
// WestEngine - RHI DX12
// DX12-specific helper macros, COM smart pointer alias, DRED utilities
// =============================================================================
#pragma once

#include "core/Assert.h"
#include "core/Logger.h"

#include <cstdint>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

namespace west::rhi
{

// ── COM Smart Pointer Alias ───────────────────────────────────────────────
template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

} // namespace west::rhi

// ── HRESULT Check Macro ───────────────────────────────────────────────────

#define WEST_HR_CHECK(hr)                                                                                              \
    do                                                                                                                 \
    {                                                                                                                  \
        HRESULT _hr = (hr);                                                                                            \
        WEST_CHECK(SUCCEEDED(_hr), "HRESULT failed: 0x{:08X}", static_cast<uint32_t>(_hr));                            \
    } while (0)
