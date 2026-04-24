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

namespace
{

[[nodiscard]] bool IsDepthFormat(RHIFormat format)
{
    return format == RHIFormat::D16_UNORM ||
           format == RHIFormat::D24_UNORM_S8_UINT ||
           format == RHIFormat::D32_FLOAT ||
           format == RHIFormat::D32_FLOAT_S8_UINT;
}

[[nodiscard]] DXGI_FORMAT ToDX12DepthResourceFormat(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::D16_UNORM:
        return DXGI_FORMAT_R16_TYPELESS;
    case RHIFormat::D24_UNORM_S8_UINT:
        return DXGI_FORMAT_R24G8_TYPELESS;
    case RHIFormat::D32_FLOAT:
        return DXGI_FORMAT_R32_TYPELESS;
    case RHIFormat::D32_FLOAT_S8_UINT:
        return DXGI_FORMAT_R32G8X24_TYPELESS;
    default:
        return static_cast<DXGI_FORMAT>(ToDXGIFormat(format));
    }
}

[[nodiscard]] DXGI_FORMAT ToDX12DepthStencilViewFormat(RHIFormat format)
{
    switch (format)
    {
    case RHIFormat::D16_UNORM:
        return DXGI_FORMAT_D16_UNORM;
    case RHIFormat::D24_UNORM_S8_UINT:
        return DXGI_FORMAT_D24_UNORM_S8_UINT;
    case RHIFormat::D32_FLOAT:
        return DXGI_FORMAT_D32_FLOAT;
    case RHIFormat::D32_FLOAT_S8_UINT:
        return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
    default:
        return static_cast<DXGI_FORMAT>(ToDXGIFormat(format));
    }
}

[[nodiscard]] DXGI_FORMAT ResolveResourceFormat(const RHITextureDesc& desc)
{
    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil) && HasFlag(desc.usage, RHITextureUsage::ShaderResource) &&
        IsDepthFormat(desc.format))
    {
        return ToDX12DepthResourceFormat(desc.format);
    }

    return static_cast<DXGI_FORMAT>(ToDXGIFormat(desc.format));
}

[[nodiscard]] D3D12_RESOURCE_DESC BuildResourceDesc(const RHITextureDesc& desc)
{
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = desc.width;
    resourceDesc.Height = desc.height;
    resourceDesc.DepthOrArraySize = static_cast<UINT16>(desc.arrayLayers);
    resourceDesc.MipLevels = static_cast<UINT16>(desc.mipLevels);
    resourceDesc.Format = ResolveResourceFormat(desc);
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

    return resourceDesc;
}

void SetDebugName(ID3D12Resource* resource, const char* debugName)
{
    if (!resource || !debugName)
    {
        return;
    }

    wchar_t wideName[128]{};
    size_t converted = 0;
    mbstowcs_s(&converted, wideName, debugName, sizeof(wideName) / sizeof(wchar_t) - 1);
    resource->SetName(wideName);
}

void CreateViewsIfNeeded(DX12Device* device, ID3D12Resource* resource, const RHITextureDesc& desc,
                         ComPtr<ID3D12DescriptorHeap>& rtvHeap, D3D12_CPU_DESCRIPTOR_HANDLE& rtvHandle,
                         ComPtr<ID3D12DescriptorHeap>& dsvHeap, D3D12_CPU_DESCRIPTOR_HANDLE& dsvHandle)
{
    if (!device || !resource)
    {
        return;
    }

    if (HasFlag(desc.usage, RHITextureUsage::RenderTarget))
    {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvHeapDesc.NumDescriptors = 1;
        rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        WEST_HR_CHECK(device->GetD3DDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&rtvHeap)));
        rtvHandle = rtvHeap->GetCPUDescriptorHandleForHeapStart();
        device->GetD3DDevice()->CreateRenderTargetView(resource, nullptr, rtvHandle);
    }

    if (HasFlag(desc.usage, RHITextureUsage::DepthStencil))
    {
        D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc{};
        dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        dsvHeapDesc.NumDescriptors = 1;
        dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        WEST_HR_CHECK(device->GetD3DDevice()->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&dsvHeap)));
        dsvHandle = dsvHeap->GetCPUDescriptorHandleForHeapStart();

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
        dsvDesc.Format = ToDX12DepthStencilViewFormat(desc.format);
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;
        device->GetD3DDevice()->CreateDepthStencilView(resource, &dsvDesc, dsvHandle);
    }
}

} // namespace

DX12Texture::~DX12Texture()
{
    if (m_isAliased && m_resource)
    {
        ID3D12Resource* resource = m_resource;
        ComPtr<ID3D12DescriptorHeap> rtvHeap = m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap> dsvHeap = m_dsvHeap;

        if (m_device)
        {
            m_device->EnqueueDeferredDeletion(
                [resource, rtvHeap, dsvHeap]() mutable {
                    if (resource)
                    {
                        resource->Release();
                    }
                    rtvHeap.Reset();
                    dsvHeap.Reset();
                },
                m_device->GetCurrentFrameFenceValue());
        }
        else
        {
            if (resource)
            {
                resource->Release();
            }
            rtvHeap.Reset();
            dsvHeap.Reset();
        }
    }
    else if (m_allocation && m_ownsResource)
    {
        D3D12MA::Allocation* allocation = m_allocation;
        ComPtr<ID3D12DescriptorHeap> rtvHeap = m_rtvHeap;
        ComPtr<ID3D12DescriptorHeap> dsvHeap = m_dsvHeap;
        if (m_device)
        {
            m_device->EnqueueDeferredDeletion(
                [allocation, rtvHeap, dsvHeap]() mutable {
                    allocation->Release();
                    rtvHeap.Reset();
                    dsvHeap.Reset();
                },
                m_device->GetCurrentFrameFenceValue());
        }
        else
        {
            allocation->Release();
            rtvHeap.Reset();
            dsvHeap.Reset();
        }
    }

    m_allocation = nullptr;
    m_resource = nullptr;
    m_aliasingAllocation.reset();
    m_rtvHeap.Reset();
    m_dsvHeap.Reset();
}

void DX12Texture::Initialize(DX12Device* device, const RHITextureDesc& desc)
{
    WEST_ASSERT(device != nullptr);
    WEST_ASSERT(desc.width > 0 && desc.height > 0 && desc.depth > 0);
    WEST_ASSERT(desc.dimension == RHITextureDim::Tex2D || desc.dimension == RHITextureDim::TexCube);

    DX12MemoryAllocator* allocator = device->GetMemoryAllocator();
    WEST_ASSERT(allocator != nullptr);

    m_device = device;
    m_desc = desc;
    m_ownsResource = true;
    m_isAliased = false;

    const D3D12_RESOURCE_DESC resourceDesc = BuildResourceDesc(desc);

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

    SetDebugName(m_resource, desc.debugName);
    CreateViewsIfNeeded(device, m_resource, desc, m_rtvHeap, m_rtvHandle, m_dsvHeap, m_dsvHandle);

    WEST_LOG_VERBOSE(LogCategory::RHI, "DX12 Texture created: {} ({}x{})",
                     desc.debugName ? desc.debugName : "unnamed", desc.width, desc.height);
}

void DX12Texture::InitializeAliased(DX12Device* device, const RHITextureDesc& desc,
                                    std::shared_ptr<D3D12MA::Allocation> aliasingAllocation)
{
    WEST_ASSERT(device != nullptr);
    WEST_ASSERT(aliasingAllocation);
    WEST_ASSERT(desc.width > 0 && desc.height > 0 && desc.depth > 0);
    WEST_ASSERT(desc.dimension == RHITextureDim::Tex2D || desc.dimension == RHITextureDim::TexCube);

    DX12MemoryAllocator* allocator = device->GetMemoryAllocator();
    WEST_ASSERT(allocator != nullptr);

    m_device = device;
    m_desc = desc;
    m_ownsResource = false;
    m_isAliased = true;
    m_aliasingAllocation = std::move(aliasingAllocation);

    const D3D12_RESOURCE_DESC resourceDesc = BuildResourceDesc(desc);
    HRESULT hr = allocator->GetAllocator()->CreateAliasingResource(
        m_aliasingAllocation.get(),
        0,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        IID_PPV_ARGS(&m_resource));
    WEST_HR_CHECK(hr);

    SetDebugName(m_resource, desc.debugName);
    CreateViewsIfNeeded(device, m_resource, desc, m_rtvHeap, m_rtvHandle, m_dsvHeap, m_dsvHandle);

    WEST_LOG_VERBOSE(LogCategory::RHI, "DX12 Aliased Texture created: {} ({}x{})",
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
