// =============================================================================
// WestEngine - Core
// Linear (Scratch) Allocator implementation
// =============================================================================
#include "core/Memory/LinearAllocator.h"

#include "core/Assert.h"
#include "core/Memory/MemoryUtils.h"

#include <cstdlib>
#include <utility>

namespace west
{

LinearAllocator::LinearAllocator(usize totalSize) : m_totalSize(totalSize), m_offset(0)
{
    WEST_ASSERT(totalSize > 0);
    m_memory = static_cast<uint8*>(std::malloc(totalSize));
    WEST_CHECK(m_memory != nullptr, "LinearAllocator: failed to allocate {} bytes", totalSize);
}

LinearAllocator::~LinearAllocator()
{
    std::free(m_memory);
    m_memory = nullptr;
}

LinearAllocator::LinearAllocator(LinearAllocator&& other) noexcept
    : m_memory(other.m_memory), m_totalSize(other.m_totalSize), m_offset(other.m_offset)
{
    other.m_memory = nullptr;
    other.m_totalSize = 0;
    other.m_offset = 0;
}

LinearAllocator& LinearAllocator::operator=(LinearAllocator&& other) noexcept
{
    if (this != &other)
    {
        std::free(m_memory);

        m_memory = other.m_memory;
        m_totalSize = other.m_totalSize;
        m_offset = other.m_offset;

        other.m_memory = nullptr;
        other.m_totalSize = 0;
        other.m_offset = 0;
    }
    return *this;
}

void* LinearAllocator::Allocate(usize size, usize alignment)
{
    WEST_ASSERT(size > 0);

    // Compute aligned address based on actual pointer (not just offset)
    usize currentAddr = reinterpret_cast<usize>(m_memory + m_offset);
    usize alignedAddr = AlignUp(currentAddr, alignment);
    usize alignedOffset = static_cast<usize>(alignedAddr - reinterpret_cast<usize>(m_memory));

    if (alignedOffset + size > m_totalSize)
    {
        // Out of memory — caller should handle
        return nullptr;
    }

    void* ptr = reinterpret_cast<void*>(alignedAddr);
    m_offset = alignedOffset + size;
    return ptr;
}

void LinearAllocator::Reset()
{
    m_offset = 0;
}

} // namespace west
