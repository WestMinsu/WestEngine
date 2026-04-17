// =============================================================================
// WestEngine - Core
// Linear (Scratch) Allocator — frame-lifetime bump allocator
// =============================================================================
#pragma once

#include "core/Types.h"

namespace west
{

/// A simple bump allocator that grows linearly.
/// Supports Allocate() and Reset() — no individual Free().
/// Ideal for per-frame scratch memory.
class LinearAllocator
{
public:
    /// @param totalSize  Total backing memory size in bytes.
    explicit LinearAllocator(usize totalSize);
    ~LinearAllocator();

    // Non-copyable, movable
    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;
    LinearAllocator(LinearAllocator&& other) noexcept;
    LinearAllocator& operator=(LinearAllocator&& other) noexcept;

    /// Allocate a block of memory with given alignment.
    /// @return Pointer to allocated memory, or nullptr if out of space.
    [[nodiscard]] void* Allocate(usize size, usize alignment = 16);

    /// Reset the allocator — all previous allocations become invalid.
    void Reset();

    /// Current used bytes.
    [[nodiscard]] usize GetUsedSize() const
    {
        return m_offset;
    }

    /// Total capacity in bytes.
    [[nodiscard]] usize GetTotalSize() const
    {
        return m_totalSize;
    }

private:
    uint8* m_memory = nullptr;
    usize m_totalSize = 0;
    usize m_offset = 0;
};

} // namespace west
