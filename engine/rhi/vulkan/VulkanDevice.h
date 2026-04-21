// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan device — instance, physical device, logical device, queues
// =============================================================================
#pragma once

#include "rhi/common/BindlessPool.h"
#include "rhi/common/DeferredDeletionQueue.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/vulkan/VulkanHelpers.h"

#include <cstdint>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct VmaAllocation_T;
using VmaAllocation = VmaAllocation_T*;

namespace west::rhi
{

class VulkanQueue;
class VulkanMemoryAllocator;

class VulkanDevice final : public IRHIDevice
{
public:
    VulkanDevice();
    ~VulkanDevice() override;

    /// Initialize the Vulkan device with the given configuration.
    [[nodiscard]] bool Initialize(const RHIDeviceConfig& config);

    // ── IRHIDevice interface ──────────────────────────────────────────
    std::unique_ptr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc) override;
    std::unique_ptr<IRHITexture> CreateTexture(const RHITextureDesc& desc) override;
    std::unique_ptr<IRHIBuffer> CreateTransientBuffer(const RHIBufferDesc& desc, uint32_t aliasSlot) override;
    std::unique_ptr<IRHITexture> CreateTransientTexture(const RHITextureDesc& desc, uint32_t aliasSlot) override;
    std::unique_ptr<IRHISampler> CreateSampler(const RHISamplerDesc& desc) override;
    std::unique_ptr<IRHIPipeline> CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) override;
    std::unique_ptr<IRHIPipeline> CreateComputePipeline(const RHIComputePipelineDesc& desc) override;
    std::unique_ptr<IRHIFence> CreateFence(uint64_t initialValue = 0) override;
    std::unique_ptr<IRHISemaphore> CreateBinarySemaphore() override;
    std::unique_ptr<IRHICommandList> CreateCommandList(RHIQueueType type) override;
    IRHIQueue* GetQueue(RHIQueueType type) override;
    std::unique_ptr<IRHISwapChain> CreateSwapChain(const RHISwapChainDesc& desc) override;

    BindlessIndex RegisterBindlessResource(IRHIBuffer* buffer) override;
    BindlessIndex RegisterBindlessResource(IRHITexture* texture) override;
    BindlessIndex RegisterBindlessResource(IRHISampler* sampler) override;
    void UnregisterBindlessResource(BindlessIndex index) override;

    void WaitIdle() override;
    RHIBackend GetBackend() const override
    {
        return RHIBackend::Vulkan;
    }
    const char* GetDeviceName() const override;
    RHIDeviceCaps GetCapabilities() const override;

    // ── Internal Accessors ────────────────────────────────────────────
    VkInstance GetVkInstance() const
    {
        return m_instance;
    }
    VkPhysicalDevice GetVkPhysicalDevice() const
    {
        return m_physicalDevice;
    }
    VkDevice GetVkDevice() const
    {
        return m_device;
    }
    VulkanMemoryAllocator* GetAllocator() const
    {
        return m_memoryAllocator.get();
    }
    VkDescriptorSetLayout GetBindlessSetLayout() const
    {
        return m_bindlessSetLayout;
    }
    VkDeviceAddress GetBindlessDescriptorBufferAddress() const
    {
        return m_bindlessDescriptorBufferAddress;
    }
    PFN_vkCmdBindDescriptorBuffersEXT GetCmdBindDescriptorBuffersEXT() const
    {
        return m_vkCmdBindDescriptorBuffersEXT;
    }
    PFN_vkCmdSetDescriptorBufferOffsetsEXT GetCmdSetDescriptorBufferOffsetsEXT() const
    {
        return m_vkCmdSetDescriptorBufferOffsetsEXT;
    }
    float GetMaxSamplerAnisotropy() const
    {
        return m_maxSamplerAnisotropy;
    }

    uint32_t GetGraphicsQueueFamily() const
    {
        return m_graphicsQueueFamily;
    }

    // ── Memory Management ─────────────────────────────────────────────
    void EnqueueDeferredDeletion(std::function<void()> deleter, uint64_t fenceValue) override
    {
        m_deletionQueue.Enqueue(std::move(deleter), fenceValue);
    }
    void FlushDeferredDeletions(uint64_t completedFenceValue) override
    {
        m_deletionQueue.Flush(completedFenceValue);
    }
    void FlushAllDeferredDeletions() override
    {
        m_deletionQueue.FlushAll();
    }
    void SetCurrentFrameFenceValue(uint64_t fenceValue) override
    {
        m_currentFenceValue = fenceValue;
    }
    uint64_t GetCurrentFrameFenceValue() const override
    {
        return m_currentFenceValue;
    }

private:
    enum class BindlessDescriptorKind : uint8_t
    {
        None,
        Texture,
        Sampler,
        Buffer
    };

    void CreateInstance(bool enableValidation);
    [[nodiscard]] bool IsValidationLayerAvailable() const;
    void SetupDebugMessenger();
    void SelectPhysicalDevice(uint32_t preferredIndex);
    void CreateLogicalDevice(bool enableValidation);
    void QueryDeviceCaps();
    void LoadDescriptorBufferFunctions();
    void CreateBindlessDescriptors();
    void DestroyBindlessDescriptors();

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    uint32_t m_graphicsQueueFamily = UINT32_MAX;

    std::unique_ptr<VulkanQueue> m_graphicsQueue;
    std::unique_ptr<VulkanQueue> m_computeQueue;
    std::unique_ptr<VulkanQueue> m_copyQueue;
    std::unique_ptr<VulkanMemoryAllocator> m_memoryAllocator;

    static constexpr uint32_t kBindlessCapacity = 4096;
    uint32_t m_bindlessCapacity = 0;
    VkDescriptorSetLayout m_bindlessSetLayout = VK_NULL_HANDLE;
    VkBuffer m_bindlessDescriptorBuffer = VK_NULL_HANDLE;
    VmaAllocation m_bindlessDescriptorAllocation = VK_NULL_HANDLE;
    void* m_bindlessDescriptorMapped = nullptr;
    VkDeviceAddress m_bindlessDescriptorBufferAddress = 0;
    VkDeviceSize m_bindlessDescriptorBufferSize = 0;
    VkDeviceSize m_textureDescriptorOffset = 0;
    VkDeviceSize m_samplerDescriptorOffset = 0;
    VkDeviceSize m_bufferDescriptorOffset = 0;
    size_t m_sampledImageDescriptorSize = 0;
    size_t m_samplerDescriptorSize = 0;
    size_t m_storageBufferDescriptorSize = 0;
    float m_maxSamplerAnisotropy = 1.0f;
    BindlessPool m_bindlessPool;
    std::vector<BindlessDescriptorKind> m_bindlessDescriptorKinds;
    std::mutex m_bindlessMutex;

    PFN_vkGetDescriptorSetLayoutSizeEXT m_vkGetDescriptorSetLayoutSizeEXT = nullptr;
    PFN_vkGetDescriptorSetLayoutBindingOffsetEXT m_vkGetDescriptorSetLayoutBindingOffsetEXT = nullptr;
    PFN_vkGetDescriptorEXT m_vkGetDescriptorEXT = nullptr;
    PFN_vkCmdBindDescriptorBuffersEXT m_vkCmdBindDescriptorBuffersEXT = nullptr;
    PFN_vkCmdSetDescriptorBufferOffsetsEXT m_vkCmdSetDescriptorBufferOffsetsEXT = nullptr;

    RHIDeviceCaps m_caps{};
    std::string m_deviceName;
    bool m_validationEnabled = false;
    bool m_deviceFaultSupported = false;

    DeferredDeletionQueue m_deletionQueue;
    uint64_t m_currentFenceValue = 0;

    // Debug callback
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        VkDebugUtilsMessageTypeFlagsEXT type,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                        void* userData);
};

} // namespace west::rhi
