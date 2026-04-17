// =============================================================================
// WestEngine - RHI DX12
// DX12 command queue implementation
// =============================================================================
#include "rhi/dx12/DX12Queue.h"

#include "rhi/dx12/DX12CommandList.h"
#include "rhi/dx12/DX12Fence.h"

namespace west::rhi
{

void DX12Queue::Initialize(ID3D12Device* device, RHIQueueType type)
{
    m_type = type;

    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    queueDesc.NodeMask = 0;

    switch (type)
    {
    case RHIQueueType::Graphics:
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        break;
    case RHIQueueType::Compute:
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
        break;
    case RHIQueueType::Copy:
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
        break;
    }

    WEST_HR_CHECK(device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_queue)));

    // Name for PIX
    const wchar_t* queueNames[] = {L"Graphics Queue", L"Compute Queue", L"Copy Queue"};
    m_queue->SetName(queueNames[static_cast<uint32_t>(type)]);

    WEST_LOG_INFO(LogCategory::RHI, "DX12 {} created.",
                  (type == RHIQueueType::Graphics)  ? "Graphics Queue"
                  : (type == RHIQueueType::Compute) ? "Compute Queue"
                                                    : "Copy Queue");
}

void DX12Queue::Submit(const RHISubmitInfo& info)
{
    WEST_ASSERT(info.commandList != nullptr);

    // Get the underlying ID3D12CommandList from our wrapper
    // DX12CommandList stores its ID3D12GraphicsCommandList which inherits from ID3D12CommandList
    // We use the GetD3DCommandList() accessor
    auto* dx12CmdList = static_cast<class DX12CommandList*>(info.commandList);

    // Execute command list — we rely on DX12CommandList exposing GetD3DCommandList()
    // Forward declaration is sufficient here; full include is in the .cpp
    ID3D12CommandList* cmdLists[] = {reinterpret_cast<ID3D12CommandList*>(dx12CmdList->GetD3DCommandList())};
    m_queue->ExecuteCommandLists(1, cmdLists);

    // Signal fence if provided
    if (info.signalFence)
    {
        auto* dx12Fence = static_cast<DX12Fence*>(info.signalFence);
        WEST_HR_CHECK(m_queue->Signal(dx12Fence->GetD3DFence(), info.signalValue));
    }
}

} // namespace west::rhi
