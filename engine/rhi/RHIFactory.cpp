// =============================================================================
// WestEngine - RHI
// RHI Factory — backend-agnostic device creation
// =============================================================================
#include "rhi/interface/RHIFactory.h"

#include "core/Logger.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHIPipeline.h"
#ifdef WEST_HAS_DX12_BACKEND
#include "rhi/dx12/DX12Device.h"
#include "rhi/dx12/DX12Queue.h"
#endif
#ifdef WEST_HAS_VULKAN_BACKEND
#include "rhi/vulkan/VulkanDevice.h"
#include "rhi/vulkan/VulkanQueue.h"
#endif

namespace west::rhi
{

std::unique_ptr<IRHIDevice> RHIFactory::CreateDevice(RHIBackend backend, const RHIDeviceConfig& config)
{
    switch (backend)
    {
    case RHIBackend::DX12:
    {
#ifdef WEST_HAS_DX12_BACKEND
        WEST_LOG_INFO(LogCategory::RHI, "Creating DX12 backend...");
        auto device = std::make_unique<DX12Device>();
        if (!device->Initialize(config))
        {
            WEST_LOG_FATAL(LogCategory::RHI, "Failed to initialize DX12 Device");
            return nullptr;
        }
        return device;
#else
        WEST_LOG_FATAL(LogCategory::RHI, "DX12 backend is not available in this build");
        return nullptr;
#endif
    }

    case RHIBackend::Vulkan:
    {
#ifdef WEST_HAS_VULKAN_BACKEND
        WEST_LOG_INFO(LogCategory::RHI, "Creating Vulkan backend...");
        auto device = std::make_unique<VulkanDevice>();
        if (!device->Initialize(config))
        {
            WEST_LOG_FATAL(LogCategory::RHI, "Failed to initialize Vulkan Device");
            return nullptr;
        }
        return device;
#else
        WEST_LOG_FATAL(LogCategory::RHI, "Vulkan backend is not available in this build");
        return nullptr;
#endif
    }

    default:
        WEST_LOG_FATAL(LogCategory::RHI, "Unknown RHI backend");
        return nullptr;
    }
}

} // namespace west::rhi
