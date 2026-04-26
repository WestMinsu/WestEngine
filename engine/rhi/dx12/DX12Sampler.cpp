// =============================================================================
// WestEngine - RHI DX12
// DX12 sampler implementation
// =============================================================================
#include "rhi/dx12/DX12Sampler.h"

#include "rhi/dx12/DX12Device.h"

namespace west::rhi
{

DX12Sampler::~DX12Sampler()
{
    if (m_device && m_bindlessIndex != kInvalidBindlessIndex)
    {
        m_device->UnregisterBindlessResource(this);
    }
}

} // namespace west::rhi
