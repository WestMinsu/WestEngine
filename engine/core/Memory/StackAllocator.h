// =============================================================================
// WestEngine - Core
// Stack Allocator — LIFO allocation with marker-based rollback
// =============================================================================
#pragma once

#include "core/Types.h"

namespace west
{

/// LIFO allocator with marker-based rollback.
/// Allocations must be freed in reverse order (or rolled back via markers).
class StackAllocator
{
public:
    using Marker = usize;

    /// @param totalSize  Total backing memory size in bytes.
    explicit StackAllocator(usize totalSize);
    ~StackAllocator();

    // Non-copyable, movable
    StackAllocator(const StackAllocator&) = delete;
    StackAllocator& operator=(const StackAllocator&) = delete;
    StackAllocator(StackAllocator&& other) noexcept;
    StackAllocator& operator=(StackAllocator&& other) noexcept;

    /// Allocate a block of memory.
    [[nodiscard]] void* Allocate(usize size, usize alignment = 16);

    /// Get a marker representing the current stack top.
    [[nodiscard]] Marker GetMarker() const { return m_offset; }

    /// Roll back all allocations made since the given marker.
    void FreeToMarker(Marker marker);

    /// Reset the entire stack.
    void Reset();

    [[nodiscard]] usize GetUsedSize() const { return m_offset; }
    [[nodiscard]] usize GetTotalSize() const { return m_totalSize; }

private:
    uint8* m_memory    = nullptr;
    usize  m_totalSize = 0;
    usize  m_offset    = 0;
};

} // namespace west
