// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan buffer implementation — VMA allocation
// =============================================================================
#include "rhi/vulkan/VulkanBuffer.h"

#include "rhi/vulkan/VulkanMemoryAllocator.h"
#include "rhi/vulkan/VulkanDevice.h"

namespace west::rhi
{

VulkanBuffer::~VulkanBuffer()
{
    if (m_mappedPtr && m_vmaAllocator)
    {
        vmaUnmapMemory(m_vmaAllocator, m_allocation);
        m_mappedPtr = nullptr;
    }

    if (m_buffer && m_vmaAllocator)
    {
        VkBuffer buffer = m_buffer;
        VmaAllocation allocation = m_allocation;
        VmaAllocator allocator = m_vmaAllocator;

        if (m_device)
        {
            m_device->EnqueueDeferredDeletion(
                [allocator, buffer, allocation]() {
                    vmaDestroyBuffer(allocator, buffer, allocation);
                },
                m_device->GetCurrentFrameFenceValue()
            );
        }
        else
        {
            // Fallback for immediate destruction
            vmaDestroyBuffer(allocator, buffer, allocation);
        }

        m_buffer = VK_NULL_HANDLE;
        m_allocation = VK_NULL_HANDLE;
    }
}

void VulkanBuffer::Initialize(VulkanDevice* device, const RHIBufferDesc& desc)
{
    WEST_ASSERT(device != nullptr);
    VulkanMemoryAllocator* allocator = device->GetAllocator();
    WEST_ASSERT(allocator != nullptr);
    WEST_ASSERT(desc.sizeBytes > 0);

    m_desc = desc;
    m_device = device;
    m_vmaAllocator = allocator->GetAllocator();

    // Convert RHIBufferUsage → VkBufferUsageFlags
    VkBufferUsageFlags usageFlags = 0;
    if (HasFlag(desc.usage, RHIBufferUsage::VertexBuffer))
        usageFlags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::IndexBuffer))
        usageFlags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::ConstantBuffer))
        usageFlags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::StorageBuffer))
        usageFlags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::IndirectArgs))
        usageFlags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::CopySource))
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::CopyDest))
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (HasFlag(desc.usage, RHIBufferUsage::AccelStructure))
        usageFlags |= VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR;

    usageFlags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    // Ensure transfer bits are set for staging operations
    if (desc.memoryType == RHIMemoryType::Upload)
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (desc.memoryType == RHIMemoryType::GPULocal)
        usageFlags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = desc.sizeBytes;
    bufferInfo.usage = usageFlags;
    device->ConfigureQueueSharing(bufferInfo);

    // VMA allocation
    VmaAllocationCreateInfo allocInfo{};

    switch (desc.memoryType)
    {
    case RHIMemoryType::GPULocal:
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;
        break;
    case RHIMemoryType::Upload:
        allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        break;
    case RHIMemoryType::Readback:
        allocInfo.usage = VMA_MEMORY_USAGE_GPU_TO_CPU;
        break;
    case RHIMemoryType::GPUShared:
        // ReBAR: prefer device-local + host-visible
        if (allocator->SupportsReBAR())
        {
            allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
            allocInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
            allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
        }
        else
        {
            // Fallback to CPU_TO_GPU (upload heap)
            allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;
        }
        break;
    }

    WEST_VK_CHECK(vmaCreateBuffer(m_vmaAllocator, &bufferInfo, &allocInfo, &m_buffer, &m_allocation, nullptr));

    WEST_LOG_VERBOSE(LogCategory::RHI, "Vulkan Buffer created: {} ({} bytes)",
                     desc.debugName ? desc.debugName : "unnamed", desc.sizeBytes);
}

void* VulkanBuffer::Map()
{
    if (m_mappedPtr)
        return m_mappedPtr;

    WEST_ASSERT(m_desc.memoryType != RHIMemoryType::GPULocal);

    VkResult result = vmaMapMemory(m_vmaAllocator, m_allocation, &m_mappedPtr);
    if (result != VK_SUCCESS)
    {
        WEST_LOG_ERROR(LogCategory::RHI, "VulkanBuffer::Map failed: {}", static_cast<int>(result));
        return nullptr;
    }

    return m_mappedPtr;
}

void VulkanBuffer::Unmap()
{
    if (!m_mappedPtr)
        return;

    vmaUnmapMemory(m_vmaAllocator, m_allocation);
    m_mappedPtr = nullptr;
}

} // namespace west::rhi
