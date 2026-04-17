// =============================================================================
// WestEngine - RHI Vulkan
// Vulkan device — instance, physical device, logical device, queues
// =============================================================================
#pragma once

#include "rhi/interface/IRHIDevice.h"
#include "rhi/vulkan/VulkanHelpers.h"

#include <string>
#include <vector>

namespace west::rhi
{

class VulkanQueue;

class VulkanDevice final : public IRHIDevice
{
public:
    VulkanDevice() = default;
    ~VulkanDevice() override;

    /// Initialize the Vulkan device with the given configuration.
    [[nodiscard]] bool Initialize(const RHIDeviceConfig& config);

    // ── IRHIDevice interface ──────────────────────────────────────────
    std::unique_ptr<IRHIBuffer> CreateBuffer(const RHIBufferDesc& desc) override;
    std::unique_ptr<IRHITexture> CreateTexture(const RHITextureDesc& desc) override;
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
    uint32_t GetGraphicsQueueFamily() const
    {
        return m_graphicsQueueFamily;
    }

private:
    void CreateInstance(bool enableValidation);
    void SetupDebugMessenger();
    void SelectPhysicalDevice(uint32_t preferredIndex);
    void CreateLogicalDevice(bool enableValidation);
    void QueryDeviceCaps();

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;

    uint32_t m_graphicsQueueFamily = UINT32_MAX;

    std::unique_ptr<VulkanQueue> m_graphicsQueue;

    RHIDeviceCaps m_caps{};
    std::string m_deviceName;
    bool m_deviceFaultSupported = false;

    // Debug callback
    static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                        VkDebugUtilsMessageTypeFlagsEXT type,
                                                        const VkDebugUtilsMessengerCallbackDataEXT* callbackData,
                                                        void* userData);
};

} // namespace west::rhi
