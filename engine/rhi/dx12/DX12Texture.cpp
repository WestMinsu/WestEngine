// =============================================================================
// WestEngine - RHI DX12
// DX12 texture implementation
// =============================================================================
#include "rhi/dx12/DX12Texture.h"

#include "rhi/common/FormatConversion.h"
#include "rhi/dx12/DX12Device.h"
#include "rhi/dx12/DX12MemoryAllocator.h"

namespace west::rhi
{

DX12Texture::~DX12Texture()
{
    if (m_allocation && m_ownsResource)
    {
        D3D12MA::Allocation* allocation = m_allocation;
        if (m_device)
        {
            m_device->EnqueueDeferredDeletion(
                [allocation]() {
                    allocation->Release();
                },
                m_device->GetCurrentFrameFenceValue());
        }
        else
        {
            allocation->Release();
        }
    }

    m_allocation = nullptr;
    m_resource = nullptr;
}

void DX12Texture::Initialize(DX12Device* device, const RHITextureDesc& desc)
{
    WEST_ASSERT(device != nullptr);
    WEST_ASSERT(desc.width > 0 && desc.height > 0 && desc.depth > 0);
    WEST_ASSERT(desc.dimension == RHITextureDim::Tex2D);

    DX12MemoryAllocator* allocator = device->GetMemoryAllocator();
    WEST_ASSERT(allocator != nullptr);

    m_device = device;
    m_desc = desc;
    m_ownsResource = true;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.arrayLayers);
    resourceDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
    resourceDesc.Format = static_cast<DXGI_FORMAT>(ToDXGIFormat(desc.format));
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    if (HasFlag(desc.usage, RHITextureUsage::RenderTarget))
    {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    }
    if (HasFlag(desc.usage, RHITextureUsage::UnorderedAccess))
    {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }
    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil))
    {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    }

    D3D12MA::ALLOCATION_DESC allocDesc{};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    HRESULT hr = allocator->GetAllocator()->CreateResource(
        &allocDesc,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        &m_allocation,
        IID_PPV_ARGS(&m_resource));
    WEST_HR_CHECK(hr);

    if (desc.debugName)
    {
        wchar_t wideName[128]{};
        size_t converted = 0;
        mbstowcs_s(&converted, wideName, desc.debugName, sizeof(wideName) / sizeof(wchar_t) - 1);
        m_resource->SetName(wideName);
    }

    WEST_LOG_VERBOSE(LogCategory::RHI, "DX12 Texture created: {} ({}x{})",
                     desc.debugName ? desc.debugName : "unnamed", desc.width, desc.height);
}

void DX12Texture::InitFromExisting(ID3D12Resource* resource, const RHITextureDesc& desc,
                                   D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle)
{
    m_resource = resource;
    m_desc = desc;
    m_rtvHandle = rtvHandle;
    m_ownsResource = false;
}

} // namespace west::rhi
