# =============================================================================
# WestEngine — Root CMake
# Single Source of Truth for build configuration
# =============================================================================
cmake_minimum_required(VERSION 3.21)

# ── vcpkg Toolchain ─────────────────────────────────────────────────────────
# Must be set BEFORE project() for vcpkg manifest mode to work.
# A stale cached path causes CMakeDetermineSystem.cmake to fail before configure.
if(DEFINED CMAKE_TOOLCHAIN_FILE AND NOT EXISTS "${CMAKE_TOOLCHAIN_FILE}")
    message(WARNING "Ignoring invalid CMAKE_TOOLCHAIN_FILE='${CMAKE_TOOLCHAIN_FILE}'.")
    unset(CMAKE_TOOLCHAIN_FILE CACHE)
    unset(CMAKE_TOOLCHAIN_FILE)
endif()

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(_west_vcpkg_toolchain_candidates
        "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake"
        "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
    )

    # Visual Studio ships vcpkg at VC/vcpkg; include it as a fallback.
    file(GLOB _west_vs_vcpkg_toolchains
        "C:/Program Files/Microsoft Visual Studio/*/*/VC/vcpkg/scripts/buildsystems/vcpkg.cmake")
    list(APPEND _west_vcpkg_toolchain_candidates ${_west_vs_vcpkg_toolchains})

    foreach(_west_vcpkg_toolchain IN LISTS _west_vcpkg_toolchain_candidates)
        if(EXISTS "${_west_vcpkg_toolchain}")
            set(CMAKE_TOOLCHAIN_FILE "${_west_vcpkg_toolchain}"
                CACHE FILEPATH "vcpkg toolchain file")
            break()
        endif()
    endforeach()

    unset(_west_vcpkg_toolchain)
    unset(_west_vs_vcpkg_toolchains)
    unset(_west_vcpkg_toolchain_candidates)
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

# ── Shader Build Pipeline ─────────────────────────────────────────────────
find_package(Python3 COMPONENTS Interpreter REQUIRED)
include(cmake/ShaderCompile.cmake)

west_add_slang_shader(
    NAME TexturedQuad
    SOURCE shaders/TexturedQuad.slang
    VERTEX VSMain
    FRAGMENT PSMain
)

west_add_slang_shader(
    NAME ToneMapping
    SOURCE shaders/ToneMapping.slang
    VERTEX VSMain
    FRAGMENT PSMain
)

west_finalize_slang_shaders()

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
        west_engine_runtime
)

# Copy dependent runtime DLLs (vcpkg/system import DLLs) next to the exe.
if(WIN32)
    add_custom_command(TARGET west_engine POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:west_engine>
            $<TARGET_FILE_DIR:west_engine>
        COMMAND_EXPAND_LISTS
    )
endif()

west_set_compiler_options(west_engine)

# Set working directory for Visual Studio debugger
set_target_properties(west_engine PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
