// =============================================================================
// WestEngine - Tests
// StackAllocator unit test
// =============================================================================
#include "core/Memory/MemoryUtils.h"
#include "core/Memory/StackAllocator.h"

#include <cassert>
#include <cstdio>

using namespace west;

int main()
{
    std::printf("=== StackAllocator Tests ===\n");

    // Test 1: Basic allocation
    {
        StackAllocator alloc(1024);
        void* p = alloc.Allocate(64);
        assert(p != nullptr);
        assert(alloc.GetUsedSize() >= 64);
        std::printf("[PASS] Basic allocation\n");
    }

    // Test 2: Marker rollback
    {
        StackAllocator alloc(1024);
        (void)alloc.Allocate(64);
        auto marker = alloc.GetMarker();
        (void)alloc.Allocate(128);
        assert(alloc.GetUsedSize() >= 192);

        alloc.FreeToMarker(marker);
        assert(alloc.GetUsedSize() == marker);
        std::printf("[PASS] Marker rollback\n");
    }

    // Test 3: Exhaustion returns nullptr
    {
        StackAllocator alloc(64);
        void* p1 = alloc.Allocate(32);
        assert(p1 != nullptr);
        void* p2 = alloc.Allocate(64); // Should fail
        assert(p2 == nullptr);
        std::printf("[PASS] Exhaustion returns nullptr\n");
    }

    // Test 4: Reset
    {
        StackAllocator alloc(256);
        (void)alloc.Allocate(256);
        alloc.Reset();
        assert(alloc.GetUsedSize() == 0);
        void* p = alloc.Allocate(128);
        assert(p != nullptr);
        std::printf("[PASS] Reset\n");
    }

    // Test 5: Alignment
    {
        StackAllocator alloc(1024);
        (void)alloc.Allocate(3, 1); // Misalign intentionally
        void* p = alloc.Allocate(16, 128);
        assert(p != nullptr);
        assert(IsAligned(reinterpret_cast<usize>(p), 128));
        std::printf("[PASS] Alignment enforcement\n");
    }

    // Test 6: Nested markers
    {
        StackAllocator alloc(1024);
        auto m0 = alloc.GetMarker();
        (void)alloc.Allocate(64);
        auto m1 = alloc.GetMarker();
        (void)alloc.Allocate(64);
        auto m2 = alloc.GetMarker();
        (void)alloc.Allocate(64);

        alloc.FreeToMarker(m2);
        assert(alloc.GetUsedSize() == m2);
        alloc.FreeToMarker(m1);
        assert(alloc.GetUsedSize() == m1);
        alloc.FreeToMarker(m0);
        assert(alloc.GetUsedSize() == 0);
        std::printf("[PASS] Nested markers\n");
    }

    std::printf("=== All StackAllocator tests passed! ===\n");
    return 0;
}
