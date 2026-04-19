# =============================================================================
# WestEngine — Engine Library Targets
# =============================================================================

# ── west_core ───────────────────────────────────────────────────────────────
# Pure C++20. NO OS API, NO Graphics API dependencies.
add_library(west_core STATIC
    core/Logger.cpp
    core/Timer.cpp
    core/Memory/LinearAllocator.cpp
    core/Memory/StackAllocator.cpp
    core/Memory/PoolAllocator.cpp
)

file(GLOB_RECURSE WEST_CORE_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/*.inl"
)

target_sources(west_core PRIVATE ${WEST_CORE_HEADERS})

target_include_directories(west_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}    # engine/ root — enables #include "core/Logger.h"
)

# Tracy Profiler — CPU telemetry (Phase 0)
if(WEST_HAS_TRACY)
    target_link_libraries(west_core
        PUBLIC
            Tracy::TracyClient
    )
endif()

# TRACY_ENABLE: active in Debug and RelWithDebInfo, inactive in Release
target_compile_definitions(west_core
    PUBLIC
        $<$<AND:$<BOOL:${WEST_HAS_TRACY}>,$<CONFIG:Debug>>:TRACY_ENABLE>
        $<$<AND:$<BOOL:${WEST_HAS_TRACY}>,$<CONFIG:RelWithDebInfo>>:TRACY_ENABLE>
)

west_set_compiler_options(west_core)

# ── west_platform_win32 ────────────────────────────────────────────────────
# Win32 implementation. Depends on west_core.
if(WIN32)
    add_library(west_platform_win32 STATIC
        platform/win32/Win32Window.cpp
    )

    file(GLOB_RECURSE WEST_PLATFORM_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/platform/*.inl"
    )

    target_sources(west_platform_win32 PRIVATE ${WEST_PLATFORM_HEADERS})

    target_link_libraries(west_platform_win32
        PUBLIC
            west_core
    )

    west_set_compiler_options(west_platform_win32)
endif()

# ── west_engine_runtime ───────────────────────────────────────────────────
# Application lifecycle and demo rendering loop. Depends on platform + RHI.
if(WIN32)
    add_library(west_engine_runtime STATIC
        platform/win32/Win32Application.cpp
        rhi/RHIFactory.cpp
    )

    target_link_libraries(west_engine_runtime
        PUBLIC
            west_core
            west_platform_win32
    )

    west_set_compiler_options(west_engine_runtime)
endif()

# ── west_rhi_interface ──────────────────────────────────────────────────────
# Pure header-only INTERFACE library. No .cpp files.
add_library(west_rhi_interface INTERFACE)

target_include_directories(west_rhi_interface
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}    # engine/ root
)

target_link_libraries(west_rhi_interface
    INTERFACE
        west_core
)

# ── west_rhi_common ─────────────────────────────────────────────────────────
# Shared utilities across backends (format conversion, etc.)
add_library(west_rhi_common STATIC
    rhi/common/FormatConversion.cpp
    rhi/common/DeferredDeletionQueue.cpp
)

file(GLOB_RECURSE WEST_RHI_COMMON_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/rhi/common/*.h"
)

target_sources(west_rhi_common PRIVATE ${WEST_RHI_COMMON_HEADERS})

target_link_libraries(west_rhi_common
    PUBLIC
        west_rhi_interface
)

west_set_compiler_options(west_rhi_common)

# ── west_rhi_dx12 ──────────────────────────────────────────────────────────
# DX12 backend implementation. Depends on west_rhi_interface + DirectX 12.
if(WIN32 AND WEST_HAS_DX12)
    add_library(west_rhi_dx12 STATIC
        rhi/dx12/DX12Device.cpp
        rhi/dx12/DX12Queue.cpp
        rhi/dx12/DX12Fence.cpp
        rhi/dx12/DX12SwapChain.cpp
        rhi/dx12/DX12CommandList.cpp
        rhi/dx12/DX12MemoryAllocator.cpp
        rhi/dx12/DX12Buffer.cpp
        rhi/dx12/DX12Pipeline.cpp
    )

    file(GLOB_RECURSE WEST_RHI_DX12_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/dx12/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/dx12/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/dx12/*.inl"
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/interface/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/interface/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/interface/*.inl"
    )

    target_sources(west_rhi_dx12 PRIVATE ${WEST_RHI_DX12_HEADERS})

    target_link_libraries(west_rhi_dx12
        PUBLIC
            west_rhi_interface
        PRIVATE
            west_rhi_common
            d3d12.lib
            dxgi.lib
            dxguid.lib
    )

    # D3D12MA (Phase 2)
    if(WEST_HAS_D3D12MA)
        target_link_libraries(west_rhi_dx12 PRIVATE GPUOpen::D3D12MemoryAllocator)
    endif()

    # NOTE: We use d3d12.h from the Windows SDK directly.
    # Do NOT link Microsoft::DirectX-Headers or DirectX-Guids here —
    # they cause dxguids.h conflicts with D3D12MA on MSVC.

    west_set_compiler_options(west_rhi_dx12)
endif()

# ── west_rhi_vulkan ─────────────────────────────────────────────────────────
# Vulkan backend implementation. Depends on west_rhi_interface + Vulkan SDK.
if(WEST_HAS_VULKAN)
    add_library(west_rhi_vulkan STATIC
        rhi/vulkan/VulkanDevice.cpp
        rhi/vulkan/VulkanQueue.cpp
        rhi/vulkan/VulkanFence.cpp
        rhi/vulkan/VulkanSemaphore.cpp
        rhi/vulkan/VulkanSwapChain.cpp
        rhi/vulkan/VulkanCommandList.cpp
        rhi/vulkan/VulkanMemoryAllocator.cpp
        rhi/vulkan/VulkanBuffer.cpp
        rhi/vulkan/VulkanPipeline.cpp
    )

    file(GLOB_RECURSE WEST_RHI_VULKAN_HEADERS CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/vulkan/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/vulkan/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/vulkan/*.inl"
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/interface/*.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/interface/*.hpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/rhi/interface/*.inl"
    )

    target_sources(west_rhi_vulkan PRIVATE ${WEST_RHI_VULKAN_HEADERS})

    target_link_libraries(west_rhi_vulkan
        PUBLIC
            west_rhi_interface
            Vulkan::Vulkan
        PRIVATE
            west_rhi_common
    )

    # VMA (Phase 2)
    if(WEST_HAS_VMA)
        target_link_libraries(west_rhi_vulkan PRIVATE GPUOpen::VulkanMemoryAllocator)
    endif()

    # Vulkan 1.3 minimum requirement
    target_compile_definitions(west_rhi_vulkan
        PRIVATE
            VK_API_VERSION_1_3
    )

    west_set_compiler_options(west_rhi_vulkan)
endif()

if(TARGET west_engine_runtime AND TARGET west_rhi_dx12)
    target_link_libraries(west_engine_runtime PRIVATE west_rhi_dx12)
    target_compile_definitions(west_engine_runtime PRIVATE WEST_HAS_DX12_BACKEND)
endif()

if(TARGET west_engine_runtime AND TARGET west_rhi_vulkan)
    target_link_libraries(west_engine_runtime PRIVATE west_rhi_vulkan)
    target_compile_definitions(west_engine_runtime PRIVATE WEST_HAS_VULKAN_BACKEND)
endif()
