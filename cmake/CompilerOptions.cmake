# =============================================================================
# WestEngine — Compiler Options
# Warning policies, optimization flags, and encoding settings
# =============================================================================

# ── Helper function to apply compiler options to a target ───────────────────
function(west_set_compiler_options TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE
            /W4             # Warning level 4
            /WX             # Warnings as errors
            /wd4100         # Unreferenced formal parameter (common in interfaces)
            /utf-8          # Source and execution charset UTF-8
            /permissive-    # Strict conformance mode
            /Zc:__cplusplus # Report correct __cplusplus value
            /Zc:preprocessor # Standard-conforming preprocessor (__VA_OPT__ support)
        )

        # Debug-specific definitions
        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:WEST_DEBUG=1>
            $<$<CONFIG:RelWithDebInfo>:WEST_DEBUG=1>
        )

        # Unicode
        target_compile_definitions(${TARGET_NAME} PRIVATE
            UNICODE
            _UNICODE
        )
    else()
        # GCC / Clang (future cross-platform)
        target_compile_options(${TARGET_NAME} PRIVATE
            -Wall -Wextra -Wpedantic -Werror
            -Wno-unused-parameter
        )

        target_compile_definitions(${TARGET_NAME} PRIVATE
            $<$<CONFIG:Debug>:WEST_DEBUG=1>
            $<$<CONFIG:RelWithDebInfo>:WEST_DEBUG=1>
        )
    endif()
endfunction()
