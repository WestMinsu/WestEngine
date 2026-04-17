# =============================================================================
# WestEngine — Root CMake
# Single Source of Truth for build configuration
# =============================================================================
cmake_minimum_required(VERSION 3.21)

# ── vcpkg Toolchain ─────────────────────────────────────────────────────────
# Must be set BEFORE project() for vcpkg manifest mode to work.
if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "vcpkg toolchain file")
endif()

project(WestEngine
    VERSION 0.1.0
    LANGUAGES CXX
)

# ── Feature Options ─────────────────────────────────────────────────────────
# WEST_ENABLE_* is user-facing. WEST_HAS_* is internal target gating.
option(WEST_ENABLE_DX12 "Enable DX12 backend" ON)
option(WEST_ENABLE_VULKAN "Enable Vulkan backend" ON)
option(WEST_ENABLE_TRACY "Enable Tracy profiler" ON)

set(WEST_HAS_DX12 ${WEST_ENABLE_DX12})
set(WEST_HAS_VULKAN ${WEST_ENABLE_VULKAN})
set(WEST_HAS_TRACY ${WEST_ENABLE_TRACY})

# ── C++20 Standard ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Global Output Directories ───────────────────────────────────────────────
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# ── Compiler Options ────────────────────────────────────────────────────────
include(cmake/CompilerOptions.cmake)
include(cmake/ThirdParty.cmake)

# ── Engine Targets ──────────────────────────────────────────────────────────
add_subdirectory(engine)

# ── Tests ───────────────────────────────────────────────────────────────────
option(WEST_BUILD_TESTS "Build unit tests" ON)
if(WEST_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ── Executable ──────────────────────────────────────────────────────────────
add_executable(west_engine main.cpp)

target_link_libraries(west_engine
    PRIVATE
        west_core
        west_platform_win32
)

if(TARGET west_rhi_dx12)
    target_link_libraries(west_engine PRIVATE west_rhi_dx12)
endif()

if(TARGET west_rhi_vulkan)
    target_link_libraries(west_engine PRIVATE west_rhi_vulkan)
endif()

west_set_compiler_options(west_engine)

# Set working directory for Visual Studio debugger
set_target_properties(west_engine PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
