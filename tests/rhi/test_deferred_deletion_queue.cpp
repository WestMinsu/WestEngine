// =============================================================================
// WestEngine Tests - RHI Common
// DeferredDeletionQueue fence-gated deletion behavior
// =============================================================================
#include "rhi/common/DeferredDeletionQueue.h"
#include "TestAssert.h"

#include <iostream>
#include <vector>

using namespace west::rhi;

int main()
{
    DeferredDeletionQueue queue;
    std::vector<int> deleted;

    queue.Enqueue([&deleted]() { deleted.push_back(1); }, 1);
    queue.Enqueue([&deleted]() { deleted.push_back(5); }, 5);
    queue.Enqueue([&deleted]() { deleted.push_back(2); }, 2);
    queue.Enqueue([&deleted]() { deleted.push_back(4); }, 4);

    queue.Flush(2);
    assert((deleted == std::vector<int>{1, 2}));

    queue.FlushAll();
    assert((deleted == std::vector<int>{1, 2, 5, 4}));

    std::cout << "DeferredDeletionQueue tests passed\n";
    return 0;
}
