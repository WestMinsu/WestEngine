// =============================================================================
// WestEngine - Core
// Pool Allocator implementation
// =============================================================================
#include "core/Memory/PoolAllocator.h"

#include "core/Assert.h"
#include "core/Memory/MemoryUtils.h"

#include <algorithm>
#include <cstdlib>

namespace west
{

PoolAllocator::PoolAllocator(usize blockSize, usize blockCount)
    : m_blockSize(std::max(blockSize, sizeof(FreeNode))) // Minimum size for free-list node
      ,
      m_blockCount(blockCount), m_freeCount(blockCount)
{
    WEST_ASSERT(blockSize > 0);
    WEST_ASSERT(blockCount > 0);

    // Ensure block size is aligned to pointer size
    m_blockSize = AlignUp(m_blockSize, alignof(FreeNode));

    m_memory = static_cast<uint8*>(std::malloc(m_blockSize * m_blockCount));
    WEST_CHECK(m_memory != nullptr, "PoolAllocator: failed to allocate {} bytes", m_blockSize * m_blockCount);

    BuildFreeList();
}

PoolAllocator::~PoolAllocator()
{
    std::free(m_memory);
    m_memory = nullptr;
}

PoolAllocator::PoolAllocator(PoolAllocator&& other) noexcept
    : m_memory(other.m_memory), m_blockSize(other.m_blockSize), m_blockCount(other.m_blockCount),
      m_freeCount(other.m_freeCount), m_freeList(other.m_freeList)
{
    other.m_memory = nullptr;
    other.m_freeList = nullptr;
    other.m_freeCount = 0;
}

PoolAllocator& PoolAllocator::operator=(PoolAllocator&& other) noexcept
{
    if (this != &other)
    {
        std::free(m_memory);

        m_memory = other.m_memory;
        m_blockSize = other.m_blockSize;
        m_blockCount = other.m_blockCount;
        m_freeCount = other.m_freeCount;
        m_freeList = other.m_freeList;

        other.m_memory = nullptr;
        other.m_freeList = nullptr;
        other.m_freeCount = 0;
    }
    return *this;
}

void* PoolAllocator::Allocate()
{
    if (m_freeList == nullptr)
    {
        return nullptr; // Pool exhausted
    }

    FreeNode* node = m_freeList;
    m_freeList = node->next;
    --m_freeCount;

    return static_cast<void*>(node);
}

void PoolAllocator::Free(void* ptr)
{
    WEST_ASSERT(ptr != nullptr);

    // Validate that the pointer belongs to this pool
    WEST_ASSERT(static_cast<uint8*>(ptr) >= m_memory &&
                static_cast<uint8*>(ptr) < m_memory + m_blockSize * m_blockCount);

    auto* node = static_cast<FreeNode*>(ptr);
    node->next = m_freeList;
    m_freeList = node;
    ++m_freeCount;
}

void PoolAllocator::Reset()
{
    m_freeCount = m_blockCount;
    BuildFreeList();
}

void PoolAllocator::BuildFreeList()
{
    m_freeList = nullptr;
    for (usize i = 0; i < m_blockCount; ++i)
    {
        auto* node = reinterpret_cast<FreeNode*>(m_memory + i * m_blockSize);
        node->next = m_freeList;
        m_freeList = node;
    }
}

} // namespace west
