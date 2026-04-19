// =============================================================================
// WestEngine - RHI Vulkan
// VMA wrapper implementation
// =============================================================================

// VMA implementation must be in exactly one .cpp file
#define VMA_IMPLEMENTATION
#include "rhi/vulkan/VulkanMemoryAllocator.h"

namespace west::rhi
{

VulkanMemoryAllocator::~VulkanMemoryAllocator()
{
    Shutdown();
}

bool VulkanMemoryAllocator::Initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device)
{
    WEST_ASSERT(instance != VK_NULL_HANDLE);
    WEST_ASSERT(physicalDevice != VK_NULL_HANDLE);
    WEST_ASSERT(device != VK_NULL_HANDLE);

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;

    WEST_VK_CHECK(vmaCreateAllocator(&allocatorInfo, &m_allocator));

    // Detect ReBAR: check for DEVICE_LOCAL + HOST_VISIBLE memory type
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);

    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        VkMemoryPropertyFlags flags = memProps.memoryTypes[i].propertyFlags;
        if ((flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) &&
            (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT))
        {
            // Check that the heap is large enough (>256MB indicates ReBAR, not small BAR)
            uint32_t heapIndex = memProps.memoryTypes[i].heapIndex;
            if (memProps.memoryHeaps[heapIndex].size > 256ull * 1024 * 1024)
            {
                m_supportsReBAR = true;
                break;
            }
        }
    }

    WEST_LOG_INFO(LogCategory::RHI, "VMA initialized. ReBAR: {}", m_supportsReBAR ? "Yes" : "No");
    return true;
}

void VulkanMemoryAllocator::Shutdown()
{
    if (m_allocator)
    {
        LogStats();
        vmaDestroyAllocator(m_allocator);
        m_allocator = VK_NULL_HANDLE;
        WEST_LOG_INFO(LogCategory::RHI, "VMA shutdown.");
    }
}

void VulkanMemoryAllocator::LogStats() const
{
    if (!m_allocator)
        return;

    VmaTotalStatistics stats{};
    vmaCalculateStatistics(m_allocator, &stats);

    auto& total = stats.total;
    WEST_LOG_INFO(LogCategory::RHI,
                  "VMA Stats — Allocations: {}, Blocks: {}, Used: {} KB, Reserved: {} KB",
                  total.statistics.allocationCount, total.statistics.blockCount,
                  total.statistics.allocationBytes / 1024, total.statistics.blockBytes / 1024);

    if (total.statistics.allocationCount > 0)
    {
        WEST_LOG_WARNING(LogCategory::RHI,
                         "VMA: {} allocations still alive at shutdown!", total.statistics.allocationCount);
    }
}

} // namespace west::rhi
