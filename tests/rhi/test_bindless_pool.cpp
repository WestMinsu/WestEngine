// =============================================================================
// WestEngine Tests - RHI Common
// BindlessPool allocation/free behavior
// =============================================================================
#include "rhi/common/BindlessPool.h"
#include "TestAssert.h"

#include <iostream>

using namespace west::rhi;

int main()
{
    BindlessPool pool(3);

    assert(pool.GetCapacity() == 3);
    assert(pool.GetAllocatedCount() == 0);

    BindlessIndex a = pool.Allocate();
    BindlessIndex b = pool.Allocate();
    BindlessIndex c = pool.Allocate();
    BindlessIndex d = pool.Allocate();

    assert(a == 0);
    assert(b == 1);
    assert(c == 2);
    assert(d == kInvalidBindlessIndex);
    assert(pool.GetAllocatedCount() == 3);
    assert(pool.IsAllocated(a));
    assert(pool.IsAllocated(b));
    assert(pool.IsAllocated(c));

    assert(pool.Free(b));
    assert(!pool.Free(b));
    assert(!pool.Free(kInvalidBindlessIndex));
    assert(pool.GetAllocatedCount() == 2);

    BindlessIndex reused = pool.Allocate();
    assert(reused == b);
    assert(pool.GetAllocatedCount() == 3);

    assert(pool.Free(a));
    assert(pool.Free(c));
    assert(pool.Free(reused));
    assert(pool.GetAllocatedCount() == 0);

    BindlessPool resourcePool(2);
    BindlessPool samplerPool(2);
    BindlessIndex resourceIndex = resourcePool.Allocate();
    BindlessIndex samplerIndex = samplerPool.Allocate();
    assert(resourceIndex == 0);
    assert(samplerIndex == 0);
    assert(resourcePool.IsAllocated(resourceIndex));
    assert(samplerPool.IsAllocated(samplerIndex));
    assert(samplerPool.Free(samplerIndex));
    assert(resourcePool.IsAllocated(resourceIndex));
    assert(resourcePool.Free(resourceIndex));

    std::cout << "BindlessPool tests passed\n";
    return 0;
}
