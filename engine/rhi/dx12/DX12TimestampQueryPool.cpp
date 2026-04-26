// =============================================================================
// WestEngine - RHI DX12
// Timestamp query pool implementation
// =============================================================================
#include "rhi/dx12/DX12TimestampQueryPool.h"

#include "rhi/dx12/DX12Device.h"
#include "rhi/dx12/DX12Queue.h"

#include <cstring>

namespace west::rhi
{

DX12TimestampQueryPool::~DX12TimestampQueryPool()
{
    if (!m_ownerDevice)
    {
        m_queryHeap.Reset();
        m_readbackBuffer.Reset();
        return;
    }

    ComPtr<ID3D12QueryHeap> queryHeap = std::move(m_queryHeap);
    ComPtr<ID3D12Resource> readbackBuffer = std::move(m_readbackBuffer);
    if (queryHeap || readbackBuffer)
    {
        m_ownerDevice->EnqueueDeferredDeletion(
            [queryHeap = std::move(queryHeap), readbackBuffer = std::move(readbackBuffer)]() mutable
            {
                queryHeap.Reset();
                readbackBuffer.Reset();
            },
            m_ownerDevice->GetCurrentFrameFenceValue());
    }
}

void DX12TimestampQueryPool::Initialize(DX12Device* device, const RHITimestampQueryPoolDesc& desc)
{
    WEST_ASSERT(device != nullptr);
    WEST_ASSERT(desc.queryCount > 0);

    m_ownerDevice = device;
    m_desc = desc;

    D3D12_QUERY_HEAP_DESC queryHeapDesc{};
    queryHeapDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryHeapDesc.Count = desc.queryCount;
    queryHeapDesc.NodeMask = 0;
    WEST_HR_CHECK(device->GetD3DDevice()->CreateQueryHeap(&queryHeapDesc, IID_PPV_ARGS(&m_queryHeap)));

    D3D12_HEAP_PROPERTIES heapProperties{};
    heapProperties.Type = D3D12_HEAP_TYPE_READBACK;
    heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProperties.CreationNodeMask = 1;
    heapProperties.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    resourceDesc.Alignment = 0;
    resourceDesc.Width = static_cast<UINT64>(desc.queryCount) * sizeof(uint64_t);
    resourceDesc.Height = 1;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.SampleDesc.Quality = 0;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    WEST_HR_CHECK(device->GetD3DDevice()->CreateCommittedResource(
        &heapProperties,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_readbackBuffer)));

    if (desc.debugName)
    {
        wchar_t wideName[128]{};
        size_t converted = 0;
        mbstowcs_s(&converted, wideName, desc.debugName, sizeof(wideName) / sizeof(wchar_t) - 1);
        m_queryHeap->SetName(wideName);
        m_readbackBuffer->SetName(wideName);
    }

    auto* queue = static_cast<DX12Queue*>(device->GetQueue(desc.queueType));
    WEST_ASSERT(queue != nullptr);

    UINT64 timestampFrequency = 0;
    WEST_HR_CHECK(queue->GetD3DQueue()->GetTimestampFrequency(&timestampFrequency));
    WEST_CHECK(timestampFrequency > 0, "DX12 timestamp frequency is zero");
    m_timestampPeriodNanoseconds = 1'000'000'000.0f / static_cast<float>(timestampFrequency);
}

bool DX12TimestampQueryPool::ReadTimestamps(uint32_t firstQuery, uint32_t queryCount,
                                            std::span<uint64_t> timestamps)
{
    if (queryCount == 0)
    {
        return true;
    }
    if (firstQuery + queryCount > m_desc.queryCount || timestamps.size() < queryCount)
    {
        return false;
    }

    const SIZE_T firstByte = static_cast<SIZE_T>(firstQuery) * sizeof(uint64_t);
    const SIZE_T byteCount = static_cast<SIZE_T>(queryCount) * sizeof(uint64_t);
    D3D12_RANGE readRange{firstByte, firstByte + byteCount};

    void* mapped = nullptr;
    HRESULT hr = m_readbackBuffer->Map(0, &readRange, &mapped);
    if (FAILED(hr) || mapped == nullptr)
    {
        WEST_LOG_WARNING(LogCategory::RHI, "DX12 timestamp readback map failed: 0x{:08X}",
                         static_cast<uint32_t>(hr));
        return false;
    }

    const auto* source = reinterpret_cast<const uint8_t*>(mapped) + firstByte;
    std::memcpy(timestamps.data(), source, byteCount);

    D3D12_RANGE writtenRange{0, 0};
    m_readbackBuffer->Unmap(0, &writtenRange);
    return true;
}

} // namespace west::rhi
