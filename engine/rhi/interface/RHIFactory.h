// =============================================================================
// WestEngine - RHI Interface
// Factory — creates backend-specific IRHIDevice instances
// =============================================================================
#pragma once

#include "rhi/interface/RHIDescriptors.h"
#include "rhi/interface/RHIEnums.h"

#include <memory>

namespace west::rhi
{

class IRHIDevice;

class RHIFactory
{
public:
    /// Creates an IRHIDevice for the specified backend.
    /// @param backend  Vulkan | DX12
    /// @param config   Device creation options (validation, GPU selection, etc.)
    /// @return Created device. nullptr + log on failure.
    static std::unique_ptr<IRHIDevice> CreateDevice(RHIBackend backend, const RHIDeviceConfig& config);
};

} // namespace west::rhi
