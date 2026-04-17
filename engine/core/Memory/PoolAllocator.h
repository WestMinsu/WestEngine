// =============================================================================
// WestEngine - Core
// Pool Allocator — fixed-size object pool with O(1) alloc/free
// =============================================================================
#pragma once

#include "core/Types.h"

namespace west
{

/// Fixed-size block pool allocator using an embedded free-list.
/// Allocate and Free are both O(1).
class PoolAllocator
{
public:
    /// @param blockSize   Size of each allocation block (minimum 8 bytes for free-list pointer).
    /// @param blockCount  Number of blocks in the pool.
    PoolAllocator(usize blockSize, usize blockCount);
    ~PoolAllocator();

    // Non-copyable, movable
    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&& other) noexcept;
    PoolAllocator& operator=(PoolAllocator&& other) noexcept;

    /// Allocate one block from the pool.
    /// @return Pointer to the block, or nullptr if pool is exhausted.
    [[nodiscard]] void* Allocate();

    /// Free a previously allocated block back to the pool.
    void Free(void* ptr);

    /// Reset the pool — all blocks become available again.
    void Reset();

    [[nodiscard]] usize GetBlockSize() const
    {
        return m_blockSize;
    }
    [[nodiscard]] usize GetBlockCount() const
    {
        return m_blockCount;
    }
    [[nodiscard]] usize GetFreeCount() const
    {
        return m_freeCount;
    }

private:
    struct FreeNode
    {
        FreeNode* next;
    };

    void BuildFreeList();

    uint8* m_memory = nullptr;
    usize m_blockSize = 0;
    usize m_blockCount = 0;
    usize m_freeCount = 0;
    FreeNode* m_freeList = nullptr;
};

} // namespace west
