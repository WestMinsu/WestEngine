// =============================================================================
// WestEngine - RHI DX12
// DX12 sampler descriptor wrapper
// =============================================================================
#pragma once

#include "rhi/interface/IRHISampler.h"

namespace west::rhi
{

class DX12Device;

class DX12Sampler final : public IRHISampler
{
public:
    explicit DX12Sampler(const RHISamplerDesc& desc)
        : m_desc(desc)
    {
    }
    ~DX12Sampler() override;
    DX12Sampler(const DX12Sampler&) = delete;
    DX12Sampler& operator=(const DX12Sampler&) = delete;
    DX12Sampler(DX12Sampler&&) = delete;
    DX12Sampler& operator=(DX12Sampler&&) = delete;

    const RHISamplerDesc& GetDesc() const override
    {
        return m_desc;
    }

    BindlessIndex GetBindlessIndex() const override
    {
        return m_bindlessIndex;
    }

    void SetBindlessIndex(BindlessIndex index)
    {
        m_bindlessIndex = index;
    }
    void SetOwnerDevice(DX12Device* device)
    {
        m_device = device;
    }

private:
    RHISamplerDesc m_desc{};
    BindlessIndex m_bindlessIndex = kInvalidBindlessIndex;
    DX12Device* m_device = nullptr;
};

} // namespace west::rhi
