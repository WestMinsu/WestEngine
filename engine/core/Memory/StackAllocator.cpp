// =============================================================================
// WestEngine - Core
// Stack Allocator implementation
// =============================================================================
#include "core/Memory/StackAllocator.h"

#include "core/Assert.h"
#include "core/Memory/MemoryUtils.h"

#include <cstdlib>

namespace west
{

StackAllocator::StackAllocator(usize totalSize) : m_totalSize(totalSize), m_offset(0)
{
    WEST_ASSERT(totalSize > 0);
    m_memory = static_cast<uint8*>(std::malloc(totalSize));
    WEST_CHECK(m_memory != nullptr, "StackAllocator: failed to allocate {} bytes", totalSize);
}

StackAllocator::~StackAllocator()
{
    std::free(m_memory);
    m_memory = nullptr;
}

StackAllocator::StackAllocator(StackAllocator&& other) noexcept
    : m_memory(other.m_memory), m_totalSize(other.m_totalSize), m_offset(other.m_offset)
{
    other.m_memory = nullptr;
    other.m_totalSize = 0;
    other.m_offset = 0;
}

StackAllocator& StackAllocator::operator=(StackAllocator&& other) noexcept
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

void* StackAllocator::Allocate(usize size, usize alignment)
{
    WEST_ASSERT(size > 0);

    // Compute aligned address based on actual pointer (not just offset)
    usize currentAddr = reinterpret_cast<usize>(m_memory + m_offset);
    usize alignedAddr = AlignUp(currentAddr, alignment);
    usize alignedOffset = static_cast<usize>(alignedAddr - reinterpret_cast<usize>(m_memory));

    if (alignedOffset + size > m_totalSize)
    {
        return nullptr;
    }

    void* ptr = reinterpret_cast<void*>(alignedAddr);
    m_offset = alignedOffset + size;
    return ptr;
}

void StackAllocator::FreeToMarker(Marker marker)
{
    WEST_ASSERT(marker <= m_offset);
    m_offset = marker;
}

void StackAllocator::Reset()
{
    m_offset = 0;
}

} // namespace west
