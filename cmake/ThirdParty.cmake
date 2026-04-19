# =============================================================================
# WestEngine — Third-Party Dependencies (vcpkg)
# =============================================================================

# ── Tracy Profiler ──────────────────────────────────────────────────────────
# Nanosecond-resolution CPU profiler for frame timing and lock contention.
# Phase 0: CPU telemetry foundation.
if(WEST_HAS_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif()

# ── DirectX 12 ──────────────────────────────────────────────────────────────
# Phase 1: DX12 backend. directx-headers provides modern D3D12 headers.
if(WIN32 AND WEST_HAS_DX12)
    find_package(directx-headers CONFIG REQUIRED)
endif()

# ── Vulkan ──────────────────────────────────────────────────────────────────
# Phase 1: Vulkan backend. Vulkan SDK headers + loader.
if(WEST_HAS_VULKAN)
    find_package(Vulkan REQUIRED)
endif()

# ── D3D12 Memory Allocator ──────────────────────────────────────────────────
# Phase 2: GPU memory sub-allocation via Placed Resources.
if(WIN32 AND WEST_HAS_DX12)
    find_package(D3D12MemoryAllocator CONFIG REQUIRED)
    set(WEST_HAS_D3D12MA TRUE)
endif()

# ── Vulkan Memory Allocator ─────────────────────────────────────────────────
# Phase 2: GPU memory sub-allocation via VMA.
if(WEST_HAS_VULKAN)
    find_package(VulkanMemoryAllocator CONFIG REQUIRED)
    set(WEST_HAS_VMA TRUE)
endif()
