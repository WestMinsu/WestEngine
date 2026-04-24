# =============================================================================
# WestEngine — Third-Party Dependencies (vcpkg)
# =============================================================================

# ── Tracy Profiler ──────────────────────────────────────────────────────────
# Nanosecond-resolution CPU profiler for frame timing and lock contention.
# Phase 0: CPU telemetry foundation.
if(WEST_HAS_TRACY)
    find_package(Tracy CONFIG REQUIRED)
endif()

# ── Dear ImGui ───────────────────────────────────────────────────────────────
# Phase 7: runtime editor/profiler GUI bootstrap.
find_package(imgui QUIET CONFIG)
if(NOT TARGET imgui::imgui)
    find_package(ImGui QUIET CONFIG)
endif()
if(TARGET imgui::imgui)
    set(WEST_HAS_IMGUI TRUE)
    set(WEST_IMGUI_TARGET imgui::imgui CACHE INTERNAL "WestEngine Dear ImGui target")
elseif(TARGET ImGui::ImGui)
    set(WEST_HAS_IMGUI TRUE)
    set(WEST_IMGUI_TARGET ImGui::ImGui CACHE INTERNAL "WestEngine Dear ImGui target")
else()
    message(FATAL_ERROR "Dear ImGui package was not found via vcpkg. Install the manifest dependency 'imgui'.")
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

# ── Asset Import / Image Decode ─────────────────────────────────────────────
# Phase 6 static-scene bring-up via temporary OBJ/FBX ingestion.
find_package(assimp CONFIG REQUIRED)
find_package(Stb REQUIRED)

# ── Canonical glTF Ingestion ────────────────────────────────────────────────
# Phase 6 Slice C: single-file cgltf parser for the long-term static-scene path.
find_path(CGLTF_INCLUDE_DIRS NAMES cgltf.h REQUIRED)
