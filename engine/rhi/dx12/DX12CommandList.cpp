// =============================================================================
// WestEngine - RHI DX12
// DX12 command list implementation — Phase 1: barrier + ClearRTV only
// =============================================================================
#include "rhi/dx12/DX12CommandList.h"

#include "rhi/dx12/DX12Texture.h"

#include <vector>

namespace west::rhi
{

void DX12CommandList::Initialize(ID3D12Device* device, RHIQueueType type)
{
    m_queueType = type;

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

    WEST_LOG_VERBOSE(LogCategory::RHI, "DX12 CommandList created.");
}

// ── Recording Lifecycle ───────────────────────────────────────────────────

void DX12CommandList::Begin()
{
    // Reset allocator first (must wait for GPU to finish using it)
    WEST_HR_CHECK(m_allocator->Reset());
    // Reset command list with the allocator
    WEST_HR_CHECK(m_cmdList->Reset(m_allocator.Get(), nullptr));
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
    if (desc.type == RHIBarrierDesc::Type::Transition)
    {
        ID3D12Resource* resource = nullptr;

        if (desc.texture)
        {
            auto* dx12Tex = static_cast<DX12Texture*>(desc.texture);
            resource = dx12Tex->GetD3DResource();
        }
        // TODO(minsu): Phase 2 — buffer barriers via DX12Buffer

        if (!resource)
        {
            WEST_LOG_WARNING(LogCategory::RHI, "ResourceBarrier: no valid resource for transition");
            return;
        }

        // Convert RHIResourceState to D3D12_RESOURCE_STATES
        auto convertState = [](RHIResourceState state) -> D3D12_RESOURCE_STATES
        {
            D3D12_RESOURCE_STATES d3dState = D3D12_RESOURCE_STATE_COMMON;

            if (HasFlag(state, RHIResourceState::RenderTarget))
                d3dState |= D3D12_RESOURCE_STATE_RENDER_TARGET;
            if (HasFlag(state, RHIResourceState::Present))
                d3dState |= D3D12_RESOURCE_STATE_PRESENT;
            if (HasFlag(state, RHIResourceState::ShaderResource))
                d3dState |= D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
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
        };

        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = resource;
        barrier.Transition.StateBefore = convertState(desc.stateBefore);
        barrier.Transition.StateAfter = convertState(desc.stateAfter);
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_cmdList->ResourceBarrier(1, &barrier);
    }
    // TODO(minsu): Phase 5 — Aliasing and UAV barriers
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
    // Phase 1: Use traditional OMSetRenderTargets + Clear
    // Phase 5+: Switch to ID3D12GraphicsCommandList4::BeginRenderPass

    // Phase 1: ClearRenderTargetView — single RTV, no depth
    if (!desc.colorAttachments.empty() && desc.colorAttachments[0].texture)
    {
        const auto& colorAttach = desc.colorAttachments[0];
        auto* dx12Tex = static_cast<DX12Texture*>(colorAttach.texture);
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = dx12Tex->GetRTV();

        if (colorAttach.loadOp == RHILoadOp::Clear)
        {
            m_cmdList->ClearRenderTargetView(rtv, colorAttach.clearColor, 0, nullptr);
        }

        m_cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
    }
}

void DX12CommandList::EndRenderPass()
{
    // Phase 1: no-op. Phase 5+: ID3D12GraphicsCommandList4::EndRenderPass
}

// ── Stub implementations (Phase 2+) ──────────────────────────────────────

void DX12CommandList::SetPipeline(IRHIPipeline* /*pipeline*/)
{
    // TODO(minsu): Phase 4 — SetPipelineState + SetGraphicsRootSignature
}

void DX12CommandList::SetPushConstants(const void* /*data*/, uint32_t /*sizeBytes*/)
{
    // TODO(minsu): Phase 3 — SetGraphicsRoot32BitConstants
}

void DX12CommandList::SetVertexBuffer(uint32_t /*slot*/, IRHIBuffer* /*buffer*/, uint64_t /*offset*/)
{
    // TODO(minsu): Phase 2 — IASetVertexBuffers
}

void DX12CommandList::SetIndexBuffer(IRHIBuffer* /*buffer*/, RHIFormat /*format*/, uint64_t /*offset*/)
{
    // TODO(minsu): Phase 2 — IASetIndexBuffer
}

void DX12CommandList::Draw(uint32_t /*vertexCount*/, uint32_t /*instanceCount*/, uint32_t /*firstVertex*/,
                           uint32_t /*firstInstance*/)
{
    // TODO(minsu): Phase 4 — DrawInstanced
}

void DX12CommandList::DrawIndexed(uint32_t /*indexCount*/, uint32_t /*instanceCount*/, uint32_t /*firstIndex*/,
                                  int32_t /*vertexOffset*/, uint32_t /*firstInstance*/)
{
    // TODO(minsu): Phase 4 — DrawIndexedInstanced
}

void DX12CommandList::DrawIndexedIndirectCount(IRHIBuffer* /*argsBuffer*/, uint64_t /*argsOffset*/,
                                               IRHIBuffer* /*countBuffer*/, uint64_t /*countOffset*/,
                                               uint32_t /*maxDrawCount*/, uint32_t /*stride*/)
{
    // TODO(minsu): Phase 6 — ExecuteIndirect
}

void DX12CommandList::Dispatch(uint32_t /*groupCountX*/, uint32_t /*groupCountY*/, uint32_t /*groupCountZ*/)
{
    // TODO(minsu): Phase 4 — Dispatch
}

void DX12CommandList::CopyBuffer(IRHIBuffer* /*src*/, uint64_t /*srcOffset*/, IRHIBuffer* /*dst*/,
                                 uint64_t /*dstOffset*/, uint64_t /*size*/)
{
    // TODO(minsu): Phase 2 — CopyBufferRegion
}

void DX12CommandList::CopyBufferToTexture(IRHIBuffer* /*src*/, IRHITexture* /*dst*/, const RHICopyRegion& /*region*/)
{
    // TODO(minsu): Phase 2 — CopyTextureRegion
}

void DX12CommandList::WriteTimestamp(IRHIBuffer* /*queryBuffer*/, uint32_t /*index*/)
{
    // TODO(minsu): Phase 7 — EndQuery for timestamp
}

} // namespace west::rhi
