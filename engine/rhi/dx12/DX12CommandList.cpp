// =============================================================================
// WestEngine - RHI DX12
// DX12 command list implementation — Phase 1: barrier + ClearRTV only
// =============================================================================
#include "rhi/dx12/DX12CommandList.h"

#include "rhi/dx12/DX12Buffer.h"
#include "rhi/dx12/DX12Pipeline.h"
#include "rhi/dx12/DX12Texture.h"
#include "rhi/common/FormatConversion.h"

#include <vector>

namespace west::rhi
{

namespace
{

[[nodiscard]] D3D12_RESOURCE_STATES ConvertResourceState(RHIResourceState state)
{
    D3D12_RESOURCE_STATES d3dState = D3D12_RESOURCE_STATE_COMMON;

    if (HasFlag(state, RHIResourceState::RenderTarget))
        d3dState |= D3D12_RESOURCE_STATE_RENDER_TARGET;
    if (HasFlag(state, RHIResourceState::Present))
        d3dState |= D3D12_RESOURCE_STATE_PRESENT;
    if (HasFlag(state, RHIResourceState::ShaderResource))
        d3dState |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    if (HasFlag(state, RHIResourceState::UnorderedAccess))
        d3dState |= D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    if (HasFlag(state, RHIResourceState::DepthStencilWrite))
        d3dState |= D3D12_RESOURCE_STATE_DEPTH_WRITE;
    if (HasFlag(state, RHIResourceState::DepthStencilRead))
        d3dState |= D3D12_RESOURCE_STATE_DEPTH_READ;
    if (HasFlag(state, RHIResourceState::CopySource))
        d3dState |= D3D12_RESOURCE_STATE_COPY_SOURCE;
    if (HasFlag(state, RHIResourceState::CopyDest))
        d3dState |= D3D12_RESOURCE_STATE_COPY_DEST;
    if (HasFlag(state, RHIResourceState::VertexBuffer))
        d3dState |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    if (HasFlag(state, RHIResourceState::IndexBuffer))
        d3dState |= D3D12_RESOURCE_STATE_INDEX_BUFFER;
    if (HasFlag(state, RHIResourceState::IndirectArgument))
        d3dState |= D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;

    return d3dState;
}

[[nodiscard]] ID3D12Resource* ResolveBarrierResource(const RHIBarrierDesc& desc)
{
    if (desc.texture)
    {
        return static_cast<DX12Texture*>(desc.texture)->GetD3DResource();
    }

    if (desc.buffer)
    {
        return static_cast<DX12Buffer*>(desc.buffer)->GetD3DResource();
    }

    return nullptr;
}

[[nodiscard]] bool TryBuildD3D12Barrier(const RHIBarrierDesc& desc, D3D12_RESOURCE_BARRIER& barrier)
{
    barrier = {};

    if (desc.type == RHIBarrierDesc::Type::Transition)
    {
        ID3D12Resource* resource = ResolveBarrierResource(desc);
        if (!resource)
        {
            WEST_LOG_WARNING(LogCategory::RHI, "ResourceBarrier: no valid resource for transition");
            return false;
        }

        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = ConvertResourceState(desc.stateBefore);
        barrier.Transition.StateAfter = ConvertResourceState(desc.stateAfter);
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        return true;
    }

    if (desc.type == RHIBarrierDesc::Type::Aliasing)
    {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        barrier.Aliasing.pResourceBefore =
            desc.aliasBefore ? static_cast<DX12Texture*>(desc.aliasBefore)->GetD3DResource() : nullptr;
        barrier.Aliasing.pResourceAfter =
            desc.aliasAfter ? static_cast<DX12Texture*>(desc.aliasAfter)->GetD3DResource() : nullptr;
        return true;
    }

    if (desc.type == RHIBarrierDesc::Type::UAV)
    {
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = ResolveBarrierResource(desc);
        return true;
    }

    return false;
}

} // namespace

void DX12CommandList::Initialize(ID3D12Device* device, RHIQueueType type, ID3D12DescriptorHeap* resourceHeap,
                                 ID3D12DescriptorHeap* samplerHeap)
{
    m_queueType = type;
    m_resourceDescriptorHeap = resourceHeap;
    m_samplerDescriptorHeap = samplerHeap;

    D3D12_COMMAND_LIST_TYPE d3dType = D3D12_COMMAND_LIST_TYPE_DIRECT;
    switch (type)
    {
    case RHIQueueType::Graphics:
        d3dType = D3D12_COMMAND_LIST_TYPE_DIRECT;
        break;
    case RHIQueueType::Compute:
        d3dType = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        break;
    case RHIQueueType::Copy:
        d3dType = D3D12_COMMAND_LIST_TYPE_COPY;
        break;
    }

    WEST_HR_CHECK(device->CreateCommandAllocator(d3dType, IID_PPV_ARGS(&m_allocator)));

    // CreateCommandList creates in open state — close immediately so Reset()/Begin() can reopen
    // We use the base ID3D12Device interface (ID3D12Device::CreateCommandList)
    ComPtr<ID3D12GraphicsCommandList> rawCmdList;
    WEST_HR_CHECK(device->CreateCommandList(0, d3dType, m_allocator.Get(), nullptr, IID_PPV_ARGS(&rawCmdList)));
    WEST_HR_CHECK(rawCmdList.As(&m_cmdList));
    WEST_HR_CHECK(m_cmdList->Close()); // Close so Begin() can Reset+Reopen

    D3D12_INDIRECT_ARGUMENT_DESC indirectArgument{};
    indirectArgument.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC signatureDesc{};
    signatureDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    signatureDesc.NumArgumentDescs = 1;
    signatureDesc.pArgumentDescs = &indirectArgument;
    WEST_HR_CHECK(device->CreateCommandSignature(&signatureDesc, nullptr,
                                                 IID_PPV_ARGS(&m_drawIndexedIndirectSignature)));

    WEST_LOG_VERBOSE(LogCategory::RHI, "DX12 CommandList created.");
}

// ── Recording Lifecycle ───────────────────────────────────────────────────

void DX12CommandList::Begin()
{
    // Reset allocator first (must wait for GPU to finish using it)
    WEST_HR_CHECK(m_allocator->Reset());
    // Reset command list with the allocator
    WEST_HR_CHECK(m_cmdList->Reset(m_allocator.Get(), nullptr));

    ID3D12DescriptorHeap* heaps[] = {m_resourceDescriptorHeap, m_samplerDescriptorHeap};
    uint32_t heapCount = 0;
    for (ID3D12DescriptorHeap* heap : heaps)
    {
        if (heap)
        {
            heaps[heapCount++] = heap;
        }
    }
    if (heapCount > 0)
    {
        m_cmdList->SetDescriptorHeaps(heapCount, heaps);
    }
}

void DX12CommandList::End()
{
    WEST_HR_CHECK(m_cmdList->Close());
}

void DX12CommandList::Reset()
{
    // Begin() handles full reset. This is a no-op for DX12.
}

// ── Barrier ───────────────────────────────────────────────────────────────

void DX12CommandList::ResourceBarrier(const RHIBarrierDesc& desc)
{
    ResourceBarriers(std::span<const RHIBarrierDesc>(&desc, 1));
}

void DX12CommandList::ResourceBarriers(std::span<const RHIBarrierDesc> descs)
{
    if (descs.empty())
    {
        return;
    }

    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(descs.size());
    for (const RHIBarrierDesc& desc : descs)
    {
        D3D12_RESOURCE_BARRIER barrier{};
        if (TryBuildD3D12Barrier(desc, barrier))
        {
            barriers.push_back(barrier);
        }
    }

    if (!barriers.empty())
    {
        m_cmdList->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    }
}

// ── Viewport & Scissor ────────────────────────────────────────────────────

void DX12CommandList::SetViewport(float x, float y, float w, float h, float minDepth, float maxDepth)
{
    D3D12_VIEWPORT viewport{};
    viewport.TopLeftX = x;
    viewport.TopLeftY = y;
    viewport.Width = w;
    viewport.Height = h;
    viewport.MinDepth = minDepth;
    viewport.MaxDepth = maxDepth;
    m_cmdList->RSSetViewports(1, &viewport);
}

void DX12CommandList::SetScissor(int32_t x, int32_t y, uint32_t w, uint32_t h)
{
    D3D12_RECT rect{};
    rect.left = x;
    rect.top = y;
    rect.right = x + static_cast<LONG>(w);
    rect.bottom = y + static_cast<LONG>(h);
    m_cmdList->RSSetScissorRects(1, &rect);
}

// ── Render Pass (Phase 1: simplified — used for ClearColor path) ──────────

void DX12CommandList::BeginRenderPass(const RHIRenderPassDesc& desc)
{
    std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtvHandles;
    rtvHandles.reserve(desc.colorAttachments.size());

    for (const RHIColorAttachment& colorAttach : desc.colorAttachments)
    {
        if (!colorAttach.texture)
        {
            continue;
        }

        auto* dx12Tex = static_cast<DX12Texture*>(colorAttach.texture);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = dx12Tex->GetRTV();
        rtvHandles.push_back(rtv);

        if (colorAttach.loadOp == RHILoadOp::Clear)
        {
            m_cmdList->ClearRenderTargetView(rtv, colorAttach.clearColor, 0, nullptr);
        }
    }

    D3D12_CPU_DESCRIPTOR_HANDLE* dsvPtr = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle{};
    if (desc.depthAttachment.texture)
    {
        auto* dx12Depth = static_cast<DX12Texture*>(desc.depthAttachment.texture);
        dsvHandle = dx12Depth->GetDSV();
        dsvPtr = &dsvHandle;

        if (desc.depthAttachment.loadOp == RHILoadOp::Clear)
        {
            m_cmdList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, desc.depthAttachment.clearDepth,
                                             desc.depthAttachment.clearStencil, 0, nullptr);
        }
    }

    if (!rtvHandles.empty() || dsvPtr)
    {
        m_cmdList->OMSetRenderTargets(static_cast<UINT>(rtvHandles.size()),
                                      rtvHandles.empty() ? nullptr : rtvHandles.data(),
                                      FALSE, dsvPtr);
    }
}

void DX12CommandList::EndRenderPass()
{
    // Phase 1: no-op. Phase 5+: ID3D12GraphicsCommandList4::EndRenderPass
}

// ── Stub implementations (Phase 2+) ──────────────────────────────────────

void DX12CommandList::SetPipeline(IRHIPipeline* pipeline)
{
    auto* dx12Pipeline = static_cast<DX12Pipeline*>(pipeline);
    WEST_ASSERT(dx12Pipeline != nullptr);
    m_currentPipelineType = dx12Pipeline->GetType();
    m_cmdList->SetPipelineState(dx12Pipeline->GetPipelineState());
    if (m_currentPipelineType == RHIPipelineType::Compute)
    {
        m_cmdList->SetComputeRootSignature(dx12Pipeline->GetRootSignature());
    }
    else
    {
        m_cmdList->SetGraphicsRootSignature(dx12Pipeline->GetRootSignature());
        m_cmdList->IASetPrimitiveTopology(dx12Pipeline->GetPrimitiveTopology());
    }
}

void DX12CommandList::SetPushConstants(const void* data, uint32_t sizeBytes)
{
    WEST_ASSERT(data != nullptr);
    WEST_ASSERT(sizeBytes > 0 && (sizeBytes % sizeof(uint32_t)) == 0);
    WEST_ASSERT(sizeBytes <= kMaxPushConstantSizeBytes);
    if (m_currentPipelineType == RHIPipelineType::Compute)
    {
        m_cmdList->SetComputeRoot32BitConstants(0, sizeBytes / sizeof(uint32_t), data, 0);
    }
    else
    {
        m_cmdList->SetGraphicsRoot32BitConstants(0, sizeBytes / sizeof(uint32_t), data, 0);
    }
}

void DX12CommandList::SetVertexBuffer(uint32_t slot, IRHIBuffer* buffer, uint64_t offset)
{
    auto* dx12Buffer = static_cast<DX12Buffer*>(buffer);
    WEST_ASSERT(dx12Buffer != nullptr);

    D3D12_VERTEX_BUFFER_VIEW vbView{};
    vbView.BufferLocation = dx12Buffer->GetGPUVirtualAddress() + offset;
    vbView.SizeInBytes = static_cast<UINT>(dx12Buffer->GetDesc().sizeBytes - offset);
    vbView.StrideInBytes = dx12Buffer->GetDesc().structureByteStride;
    m_cmdList->IASetVertexBuffers(slot, 1, &vbView);
}

void DX12CommandList::SetIndexBuffer(IRHIBuffer* buffer, RHIFormat format, uint64_t offset)
{
    auto* dx12Buffer = static_cast<DX12Buffer*>(buffer);
    WEST_ASSERT(dx12Buffer != nullptr);

    D3D12_INDEX_BUFFER_VIEW ibView{};
    ibView.BufferLocation = dx12Buffer->GetGPUVirtualAddress() + offset;
    ibView.SizeInBytes = static_cast<UINT>(dx12Buffer->GetDesc().sizeBytes - offset);
    ibView.Format = static_cast<DXGI_FORMAT>(ToDXGIFormat(format));
    m_cmdList->IASetIndexBuffer(&ibView);
}

void DX12CommandList::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex,
                           uint32_t firstInstance)
{
    m_cmdList->DrawInstanced(vertexCount, instanceCount, firstVertex, firstInstance);
}

void DX12CommandList::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex,
                                  int32_t vertexOffset, uint32_t firstInstance)
{
    m_cmdList->DrawIndexedInstanced(indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void DX12CommandList::DrawIndexedIndirectCount(IRHIBuffer* argsBuffer, uint64_t argsOffset,
                                               IRHIBuffer* countBuffer, uint64_t countOffset,
                                               uint32_t maxDrawCount, uint32_t stride)
{
    WEST_ASSERT(argsBuffer != nullptr);
    WEST_ASSERT(countBuffer != nullptr);
    WEST_ASSERT(m_drawIndexedIndirectSignature != nullptr);
    WEST_ASSERT(stride == sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));

    auto* dx12ArgsBuffer = static_cast<DX12Buffer*>(argsBuffer);
    auto* dx12CountBuffer = static_cast<DX12Buffer*>(countBuffer);
    WEST_ASSERT(dx12ArgsBuffer != nullptr);
    WEST_ASSERT(dx12CountBuffer != nullptr);

    m_cmdList->ExecuteIndirect(m_drawIndexedIndirectSignature.Get(), maxDrawCount,
                               dx12ArgsBuffer->GetD3DResource(), argsOffset,
                               dx12CountBuffer->GetD3DResource(), countOffset);
}

void DX12CommandList::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    m_cmdList->Dispatch(groupCountX, groupCountY, groupCountZ);
}

void DX12CommandList::CopyBuffer(IRHIBuffer* src, uint64_t srcOffset, IRHIBuffer* dst,
                                 uint64_t dstOffset, uint64_t size)
{
    auto* dx12Src = static_cast<DX12Buffer*>(src);
    auto* dx12Dst = static_cast<DX12Buffer*>(dst);
    WEST_ASSERT(dx12Src != nullptr && dx12Dst != nullptr);

    m_cmdList->CopyBufferRegion(dx12Dst->GetD3DResource(), dstOffset,
                                dx12Src->GetD3DResource(), srcOffset, size);
}

void DX12CommandList::CopyBufferToTexture(IRHIBuffer* src, IRHITexture* dst, const RHICopyRegion& region)
{
    auto* dx12Src = static_cast<DX12Buffer*>(src);
    auto* dx12Dst = static_cast<DX12Texture*>(dst);
    WEST_ASSERT(dx12Src != nullptr && dx12Dst != nullptr);

    const RHITextureDesc& desc = dx12Dst->GetDesc();
    const uint32_t bytesPerPixel = GetFormatByteSize(desc.format);
    WEST_ASSERT(bytesPerPixel > 0);

    D3D12_TEXTURE_COPY_LOCATION srcLocation{};
    srcLocation.pResource = dx12Src->GetD3DResource();
    srcLocation.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLocation.PlacedFootprint.Offset = region.bufferOffset;
    srcLocation.PlacedFootprint.Footprint.Format = static_cast<DXGI_FORMAT>(ToDXGIFormat(desc.format));
    srcLocation.PlacedFootprint.Footprint.Width = region.texWidth;
    srcLocation.PlacedFootprint.Footprint.Height = region.texHeight;
    srcLocation.PlacedFootprint.Footprint.Depth = region.texDepth;

    const uint32_t rowLength = region.bufferRowLength == 0 ? region.texWidth : region.bufferRowLength;
    const uint32_t rowPitch = rowLength * bytesPerPixel;
    srcLocation.PlacedFootprint.Footprint.RowPitch =
        (rowPitch + D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1) & ~(D3D12_TEXTURE_DATA_PITCH_ALIGNMENT - 1);

    D3D12_TEXTURE_COPY_LOCATION dstLocation{};
    dstLocation.pResource = dx12Dst->GetD3DResource();
    dstLocation.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLocation.SubresourceIndex = region.mipLevel + region.arrayLayer * desc.mipLevels;

    D3D12_BOX srcBox{};
    srcBox.left = 0;
    srcBox.top = 0;
    srcBox.front = 0;
    srcBox.right = region.texWidth;
    srcBox.bottom = region.texHeight;
    srcBox.back = region.texDepth;

    m_cmdList->CopyTextureRegion(&dstLocation, region.texOffsetX, region.texOffsetY, region.texOffsetZ,
                                 &srcLocation, &srcBox);
}

void DX12CommandList::WriteTimestamp(IRHIBuffer* /*queryBuffer*/, uint32_t /*index*/)
{
    // TODO(minsu): Phase 7 — EndQuery for timestamp
}

} // namespace west::rhi
