// =============================================================================
// WestEngine - RHI Vulkan
// VMA (Vulkan Memory Allocator) wrapper
// =============================================================================
#pragma once

#include "rhi/vulkan/VulkanHelpers.h"

// VMA requires these macros before include
#include <vk_mem_alloc.h>

namespace west::rhi
{

class VulkanMemoryAllocator
{
public:
    VulkanMemoryAllocator() = default;
    ~VulkanMemoryAllocator();

    VulkanMemoryAllocator(const VulkanMemoryAllocator&) = delete;
    VulkanMemoryAllocator& operator=(const VulkanMemoryAllocator&) = delete;

    [[nodiscard]] bool Initialize(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device);
    void Shutdown();

    VmaAllocator GetAllocator() const { return m_allocator; }
    bool SupportsReBAR() const { return m_supportsReBAR; }

    void LogStats() const;

private:
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    bool m_supportsReBAR = false;
};

} // namespace west::rhi
