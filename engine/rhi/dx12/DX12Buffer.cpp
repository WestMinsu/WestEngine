// =============================================================================
// WestEngine - RHI DX12
// DX12 buffer implementation — D3D12MA Placed Resource allocation
// =============================================================================
#include "rhi/dx12/DX12Buffer.h"

#include "rhi/dx12/DX12MemoryAllocator.h"
#include "rhi/dx12/DX12Device.h"

namespace west::rhi
{

DX12Buffer::~DX12Buffer()
{
    if (m_mappedPtr && m_resource)
    {
        m_resource->Unmap(0, nullptr);
        m_mappedPtr = nullptr;
    }

    if (m_allocation)
    {
        D3D12MA::Allocation* alloc = m_allocation;
        if (m_device)
        {
            m_device->EnqueueDeferredDeletion(
                [alloc]() {
                    alloc->Release();
                },
                m_device->GetCurrentFrameFenceValue()
            );
        }
        else
        {
            alloc->Release();
        }

        m_allocation = nullptr;
        m_resource = nullptr;
    }
}

void DX12Buffer::Initialize(DX12Device* device, const RHIBufferDesc& desc)
{
    WEST_ASSERT(device != nullptr);
    DX12MemoryAllocator* allocator = device->GetMemoryAllocator();
    WEST_ASSERT(allocator != nullptr);
    WEST_ASSERT(desc.sizeBytes > 0);

    m_desc = desc;
    m_device = device;

    // Map RHIMemoryType → D3D12 heap type
    D3D12_HEAP_TYPE heapType = D3D12_HEAP_TYPE_DEFAULT;
    switch (desc.memoryType)
    {
    case RHIMemoryType::GPULocal:
        heapType = D3D12_HEAP_TYPE_DEFAULT;
        break;
    case RHIMemoryType::Upload:
        heapType = D3D12_HEAP_TYPE_UPLOAD;
        break;
    case RHIMemoryType::Readback:
        heapType = D3D12_HEAP_TYPE_READBACK;
        break;
    case RHIMemoryType::GPUShared:
        // ReBAR: Use custom heap with WRITE_COMBINE if supported, else fallback to Upload
        if (allocator->SupportsReBAR())
        {
            heapType = D3D12_HEAP_TYPE_DEFAULT;
            // NOTE: For true ReBAR, we'd use a custom heap. D3D12MA handles this
            // via D3D12_HEAP_TYPE_CUSTOM with WRITE_COMBINE. For simplicity in Phase 2,
            // we use UPLOAD as a reasonable approximation.
            heapType = D3D12_HEAP_TYPE_UPLOAD;
            WEST_LOG_VERBOSE(LogCategory::RHI, "GPUShared buffer: using Upload heap (ReBAR available)");
        }
        else
        {
            heapType = D3D12_HEAP_TYPE_UPLOAD;
            WEST_LOG_VERBOSE(LogCategory::RHI, "GPUShared buffer: fallback to Upload heap (no ReBAR)");
        }
        break;
    }

    // Resource description
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0; // Default alignment
    resourceDesc.Width = desc.sizeBytes;
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    // Resource flags from usage
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;
    if (HasFlag(desc.usage, RHIBufferUsage::StorageBuffer))
    {
        resourceDesc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    }

    // Initial state
    D3D12_RESOURCE_STATES initialState = D3D12_RESOURCE_STATE_COMMON;
    if (heapType == D3D12_HEAP_TYPE_UPLOAD)
    {
        initialState = D3D12_RESOURCE_STATE_GENERIC_READ;
    }
    else if (heapType == D3D12_HEAP_TYPE_READBACK)
    {
        initialState = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    // D3D12MA allocation
    D3D12MA::ALLOCATION_DESC allocDesc{};
    allocDesc.HeapType = heapType;

    HRESULT hr = allocator->GetAllocator()->CreateResource(
        &allocDesc,
        &resourceDesc,
        initialState,
        nullptr, // No optimized clear value for buffers
        &m_allocation,
        IID_PPV_ARGS(&m_resource));

    WEST_HR_CHECK(hr);

    // Set debug name
    if (desc.debugName)
    {
        wchar_t wideName[128]{};
        size_t converted = 0;
        mbstowcs_s(&converted, wideName, desc.debugName, sizeof(wideName) / sizeof(wchar_t) - 1);
        m_resource->SetName(wideName);
    }

    WEST_LOG_VERBOSE(LogCategory::RHI, "DX12 Buffer created: {} ({} bytes, heap={})",
                     desc.debugName ? desc.debugName : "unnamed", desc.sizeBytes,
                     heapType == D3D12_HEAP_TYPE_DEFAULT ? "DEFAULT" :
                     heapType == D3D12_HEAP_TYPE_UPLOAD ? "UPLOAD" : "READBACK");
}

void* DX12Buffer::Map()
{
    if (m_mappedPtr)
        return m_mappedPtr;

    WEST_ASSERT(m_desc.memoryType != RHIMemoryType::GPULocal);

    D3D12_RANGE readRange{0, 0}; // We don't intend to read from this resource on the CPU
    if (m_desc.memoryType == RHIMemoryType::Readback)
    {
        readRange = {0, static_cast<SIZE_T>(m_desc.sizeBytes)};
    }

    HRESULT hr = m_resource->Map(0, &readRange, &m_mappedPtr);
    if (FAILED(hr))
    {
        WEST_LOG_ERROR(LogCategory::RHI, "DX12Buffer::Map failed: 0x{:08X}", static_cast<uint32_t>(hr));
        return nullptr;
    }

    return m_mappedPtr;
}

void DX12Buffer::Unmap()
{
    if (!m_mappedPtr)
        return;

    D3D12_RANGE writtenRange{0, static_cast<SIZE_T>(m_desc.sizeBytes)};
    if (m_desc.memoryType == RHIMemoryType::Readback)
    {
        writtenRange = {0, 0}; // CPU didn't write
    }

    m_resource->Unmap(0, &writtenRange);
    m_mappedPtr = nullptr;
}

D3D12_GPU_VIRTUAL_ADDRESS DX12Buffer::GetGPUVirtualAddress() const
{
    return m_resource ? m_resource->GetGPUVirtualAddress() : 0;
}

} // namespace west::rhi
