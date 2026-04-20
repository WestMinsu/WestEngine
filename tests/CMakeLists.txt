# =============================================================================
# WestEngine — Unit Tests
# =============================================================================

# Use a simple test runner without external test framework dependency
# Each test file has its own main() with assert-based verification.
# Tests include TestAssert.h, which keeps C assert active in Release without
# adding compiler command-line override warnings.

# Helper: apply compiler options
function(west_add_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE west_core)
    target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    west_set_compiler_options(${TEST_NAME})
    if(MSVC)
        target_compile_options(${TEST_NAME} PRIVATE
            $<$<CONFIG:Release>:/wd4834>    # discarding [[nodiscard]] return value
        )
    endif()
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

function(west_add_rhi_common_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE west_rhi_common)
    target_include_directories(${TEST_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    west_set_compiler_options(${TEST_NAME})
    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# ── Allocator Tests ─────────────────────────────────────────────────────────
west_add_test(LinearAllocator  core/test_linear_allocator.cpp)
west_add_test(StackAllocator   core/test_stack_allocator.cpp)
west_add_test(PoolAllocator    core/test_pool_allocator.cpp)

# ── RHI Common Tests ──────────────────────────────────────────────────────
west_add_rhi_common_test(BindlessPool rhi/test_bindless_pool.cpp)
west_add_rhi_common_test(DeferredDeletionQueue rhi/test_deferred_deletion_queue.cpp)
