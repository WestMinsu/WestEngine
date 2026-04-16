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

target_include_directories(west_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}    # engine/ root — enables #include "core/Logger.h"
)

# Tracy Profiler — CPU telemetry (Phase 0)
target_link_libraries(west_core
    PUBLIC
        Tracy::TracyClient
)

# TRACY_ENABLE: active in Debug and RelWithDebInfo, inactive in Release
target_compile_definitions(west_core
    PUBLIC
        $<$<CONFIG:Debug>:TRACY_ENABLE>
        $<$<CONFIG:RelWithDebInfo>:TRACY_ENABLE>
)

west_set_compiler_options(west_core)

# ── west_platform_win32 ────────────────────────────────────────────────────
# Win32 implementation. Depends on west_core.
if(WIN32)
    add_library(west_platform_win32 STATIC
        platform/win32/Win32Window.cpp
        platform/win32/Win32Application.cpp
    )

    target_link_libraries(west_platform_win32
        PUBLIC
            west_core
    )

    west_set_compiler_options(west_platform_win32)
endif()
