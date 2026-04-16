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

west_set_compiler_options(west_engine)
