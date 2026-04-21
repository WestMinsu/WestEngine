// =============================================================================
// WestEngine - RHI Common
// Bindless index pool implementation
// =============================================================================
#include "rhi/common/BindlessPool.h"

namespace west::rhi
{

BindlessPool::BindlessPool(uint32 capacity)
{
    Initialize(capacity);
}

void BindlessPool::Initialize(uint32 capacity)
{
    m_freeList.clear();
    m_allocated.clear();
    m_allocated.resize(capacity, 0);
    m_allocatedCount = 0;

    m_freeList.reserve(capacity);
    for (uint32 i = 0; i < capacity; ++i)
    {
        m_freeList.push_back(capacity - 1 - i);
    }
}

BindlessIndex BindlessPool::Allocate()
{
    if (m_freeList.empty())
    {
        return kInvalidBindlessIndex;
    }

    BindlessIndex index = m_freeList.back();
    m_freeList.pop_back();

    m_allocated[index] = 1;
    ++m_allocatedCount;
    return index;
}

bool BindlessPool::Free(BindlessIndex index)
{
    if (index == kInvalidBindlessIndex || index >= m_allocated.size() || m_allocated[index] == 0)
    {
        return false;
    }

    m_allocated[index] = 0;
    --m_allocatedCount;
    m_freeList.push_back(index);
    return true;
}

bool BindlessPool::IsAllocated(BindlessIndex index) const
{
    if (index == kInvalidBindlessIndex || index >= m_allocated.size())
    {
        return false;
    }

    return m_allocated[index] != 0;
}

uint32 BindlessPool::GetCapacity() const
{
    return static_cast<uint32>(m_allocated.size());
}

uint32 BindlessPool::GetAllocatedCount() const
{
    return m_allocatedCount;
}

} // namespace west::rhi
