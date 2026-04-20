// =============================================================================
// WestEngine - Tests
// LinearAllocator unit test
// =============================================================================
#include "core/Memory/LinearAllocator.h"
#include "core/Memory/MemoryUtils.h"
#include "TestAssert.h"

#include <cstdio>
#include <utility>

using namespace west;

int main()
{
    std::printf("=== LinearAllocator Tests ===\n");

    // Test 1: Basic allocation
    {
        LinearAllocator alloc(1024);
        void* p1 = alloc.Allocate(64);
        assert(p1 != nullptr);
        assert(alloc.GetUsedSize() >= 64);
        std::printf("[PASS] Basic allocation\n");
    }

    // Test 2: Alignment
    {
        LinearAllocator alloc(1024);
        (void)alloc.Allocate(1, 1);       // 1 byte, 1 alignment (discard)
        void* p2 = alloc.Allocate(1, 64); // 1 byte, 64 alignment
        assert(p2 != nullptr);
        assert(IsAligned(reinterpret_cast<usize>(p2), 64));
        std::printf("[PASS] Alignment enforcement\n");
    }

    // Test 3: Exhaustion returns nullptr
    {
        LinearAllocator alloc(64);
        void* p = alloc.Allocate(32);
        assert(p != nullptr);
        void* p2 = alloc.Allocate(64); // Should fail — not enough space
        assert(p2 == nullptr);
        std::printf("[PASS] Exhaustion returns nullptr\n");
    }

    // Test 4: Reset allows reuse
    {
        LinearAllocator alloc(128);
        (void)alloc.Allocate(128);
        assert(alloc.GetUsedSize() >= 128);
        alloc.Reset();
        assert(alloc.GetUsedSize() == 0);
        void* p = alloc.Allocate(64);
        assert(p != nullptr);
        std::printf("[PASS] Reset allows reuse\n");
    }

    // Test 5: Multiple allocations fill up
    {
        LinearAllocator alloc(256);
        [[maybe_unused]] int count = 0;
        while (alloc.Allocate(32, 1) != nullptr)
        {
            ++count;
        }
        assert(count == 8); // 256 / 32 = 8
        std::printf("[PASS] Sequential fill to capacity (%d allocations)\n", count);
    }

    // Test 6: Move semantics
    {
        LinearAllocator alloc1(512);
        (void)alloc1.Allocate(128);
        usize used = alloc1.GetUsedSize();

        LinearAllocator alloc2(std::move(alloc1));
        assert(alloc2.GetUsedSize() == used);
        assert(alloc2.GetTotalSize() == 512);
        assert(alloc1.GetTotalSize() == 0);
        std::printf("[PASS] Move semantics\n");
    }

    std::printf("=== All LinearAllocator tests passed! ===\n");
    return 0;
}
