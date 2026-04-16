# =============================================================================
# WestEngine — Unit Tests
# =============================================================================

# Use a simple test runner without external test framework dependency
# Each test file has its own main() with assert-based verification

# Helper: apply compiler options + suppress unused-variable warnings in Release
# (assert-only variables become unused when assert() is compiled out)
function(west_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE west_core)
    west_set_compiler_options(${TEST_NAME})
    if(MSVC)
        target_compile_options(${TEST_NAME} PRIVATE
            $<$<CONFIG:Release>:/wd4189>    # unused local variable (assert-only)
            $<$<CONFIG:Release>:/wd4834>    # discarding [[nodiscard]] return value
        )
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# ── Allocator Tests ─────────────────────────────────────────────────────────
west_add_test(LinearAllocator  core/test_linear_allocator.cpp)
west_add_test(StackAllocator   core/test_stack_allocator.cpp)
west_add_test(PoolAllocator    core/test_pool_allocator.cpp)

