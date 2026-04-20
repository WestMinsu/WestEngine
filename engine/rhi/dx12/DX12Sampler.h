// =============================================================================
// WestEngine - RHI DX12
// DX12 sampler descriptor wrapper
// =============================================================================
#pragma once

#include "rhi/interface/IRHISampler.h"

namespace west::rhi
{

class DX12Sampler final : public IRHISampler
{
public:
    explicit DX12Sampler(const RHISamplerDesc& desc)
        : m_desc(desc)
    {
    }

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

private:
    RHISamplerDesc m_desc{};
    BindlessIndex m_bindlessIndex = kInvalidBindlessIndex;
};

} // namespace west::rhi
