// =============================================================================
// WestEngine - Tests
// PoolAllocator unit test
// =============================================================================
#include "core/Memory/PoolAllocator.h"
#include "TestAssert.h"

#include <cstdio>
#include <vector>

using namespace west;

int main()
{
    std::printf("=== PoolAllocator Tests ===\n");

    // Test 1: Basic allocate and free
    {
        PoolAllocator pool(64, 10);
        void* p = pool.Allocate();
        assert(p != nullptr);
        assert(pool.GetFreeCount() == 9);
        pool.Free(p);
        assert(pool.GetFreeCount() == 10);
        std::printf("[PASS] Basic allocate and free\n");
    }

    // Test 2: Exhaust all blocks
    {
        PoolAllocator pool(32, 5);
        std::vector<void*> ptrs;
        for (int i = 0; i < 5; ++i)
        {
            void* p = pool.Allocate();
            assert(p != nullptr);
            ptrs.push_back(p);
        }
        assert(pool.GetFreeCount() == 0);

        // Next allocation should fail
        void* pNull = pool.Allocate();
        assert(pNull == nullptr);
        std::printf("[PASS] Exhaust all blocks\n");

        // Free all
        for (auto* p : ptrs)
        {
            pool.Free(p);
        }
        assert(pool.GetFreeCount() == 5);
        std::printf("[PASS] Free all blocks\n");
    }

    // Test 3: Unique pointers (no aliasing)
    {
        PoolAllocator pool(64, 8);
        std::vector<void*> ptrs;
        for (int i = 0; i < 8; ++i)
        {
            ptrs.push_back(pool.Allocate());
        }
        // Check all pointers are unique
        for (usize i = 0; i < ptrs.size(); ++i)
        {
            for (usize j = i + 1; j < ptrs.size(); ++j)
            {
                assert(ptrs[i] != ptrs[j]);
            }
        }
        std::printf("[PASS] All pointers are unique\n");

        for (auto* p : ptrs)
        {
            pool.Free(p);
        }
    }

    // Test 4: Reset
    {
        PoolAllocator pool(32, 4);
        (void)pool.Allocate();
        (void)pool.Allocate();
        assert(pool.GetFreeCount() == 2);
        pool.Reset();
        assert(pool.GetFreeCount() == 4);
        std::printf("[PASS] Reset\n");
    }

    // Test 5: Minimum block size enforcement (smaller than FreeNode)
    {
        // Even if we request 1-byte blocks, the allocator should enforce
        // a minimum of sizeof(FreeNode) = sizeof(void*) = 8 bytes
        PoolAllocator pool(1, 4);
        assert(pool.GetBlockSize() >= sizeof(void*));
        void* p = pool.Allocate();
        assert(p != nullptr);
        pool.Free(p);
        std::printf("[PASS] Minimum block size enforcement\n");
    }

    // Test 6: Reuse freed block
    {
        PoolAllocator pool(64, 2);
        void* p1 = pool.Allocate();
        void* p2 = pool.Allocate();
        assert(pool.GetFreeCount() == 0);

        pool.Free(p1);
        assert(pool.GetFreeCount() == 1);

        void* p3 = pool.Allocate();
        assert(p3 == p1); // Should reuse the freed block (LIFO free-list)
        std::printf("[PASS] Reuse freed block\n");

        pool.Free(p2);
        pool.Free(p3);
    }

    std::printf("=== All PoolAllocator tests passed! ===\n");
    return 0;
}
