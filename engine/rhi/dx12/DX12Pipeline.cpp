// =============================================================================
// WestEngine - RHI DX12
// DX12 pipeline implementation — Phase 2 minimal PSO
// =============================================================================
#include "rhi/dx12/DX12Pipeline.h"

#include "rhi/common/FormatConversion.h"
#include "rhi/interface/RHIDescriptors.h"

#include <d3d12sdklayers.h>
#include <vector>

namespace west::rhi
{

static void LogD3D12InfoQueueMessages(ID3D12Device* device, const char* context)
{
    ComPtr<ID3D12InfoQueue> infoQueue;
    if (!device || FAILED(device->QueryInterface(IID_PPV_ARGS(&infoQueue))))
    {
        return;
    }

    const UINT64 messageCount = infoQueue->GetNumStoredMessages();
    const UINT64 firstMessage = messageCount > 16 ? messageCount - 16 : 0;

    for (UINT64 i = firstMessage; i < messageCount; ++i)
    {
        SIZE_T messageLength = 0;
        if (FAILED(infoQueue->GetMessage(i, nullptr, &messageLength)) || messageLength == 0)
        {
            continue;
        }

        std::vector<uint8_t> messageStorage(messageLength);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(messageStorage.data());
        if (SUCCEEDED(infoQueue->GetMessage(i, message, &messageLength)))
        {
            WEST_LOG_ERROR(LogCategory::RHI, "D3D12 {}: {}", context, message->pDescription);
        }
    }
}

static D3D12_COMPARISON_FUNC ToD3D12CompareOp(RHICompareOp op)
{
    switch (op)
    {
    case RHICompareOp::Never:
        return D3D12_COMPARISON_FUNC_NEVER;
    case RHICompareOp::Less:
        return D3D12_COMPARISON_FUNC_LESS;
    case RHICompareOp::Equal:
        return D3D12_COMPARISON_FUNC_EQUAL;
    case RHICompareOp::LessEqual:
        return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case RHICompareOp::Greater:
        return D3D12_COMPARISON_FUNC_GREATER;
    case RHICompareOp::NotEqual:
        return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case RHICompareOp::GreaterEqual:
        return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case RHICompareOp::Always:
    default:
        return D3D12_COMPARISON_FUNC_ALWAYS;
    }
}

static D3D12_PRIMITIVE_TOPOLOGY_TYPE ToD3D12PrimitiveTopologyType(RHIPrimitiveTopology topology)
{
    switch (topology)
    {
    case RHIPrimitiveTopology::LineList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case RHIPrimitiveTopology::PointList:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case RHIPrimitiveTopology::TriangleList:
    case RHIPrimitiveTopology::TriangleStrip:
    default:
        return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    }
}

static D3D_PRIMITIVE_TOPOLOGY ToD3D12PrimitiveTopology(RHIPrimitiveTopology topology)
{
    switch (topology)
    {
    case RHIPrimitiveTopology::TriangleStrip:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
    case RHIPrimitiveTopology::LineList:
        return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
    case RHIPrimitiveTopology::PointList:
        return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
    case RHIPrimitiveTopology::TriangleList:
    default:
        return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    }
}

void DX12Pipeline::CreateRootSignature(ID3D12Device* device)
{
    D3D12_ROOT_PARAMETER1 rootParams[1]{};
    rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParams[0].Constants.ShaderRegister = 0;
    rootParams[0].Constants.RegisterSpace = 0;
    rootParams[0].Constants.Num32BitValues = 2; // textureIndex, samplerIndex
    rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    flags |= D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED;
    flags |= D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = 1;
    rootSigDesc.Desc_1_1.pParameters = rootParams;
    rootSigDesc.Desc_1_1.NumStaticSamplers = 0;
    rootSigDesc.Desc_1_1.pStaticSamplers = nullptr;
    rootSigDesc.Desc_1_1.Flags = flags;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signature, &error);
    if (FAILED(hr))
    {
        if (error)
        {
            WEST_LOG_ERROR(LogCategory::RHI, "Root Signature serialization failed: {}",
                           static_cast<const char*>(error->GetBufferPointer()));
        }
        WEST_HR_CHECK(hr);
    }

    const HRESULT rootSignatureResult = device->CreateRootSignature(0, signature->GetBufferPointer(),
                                                                    signature->GetBufferSize(),
                                                                    IID_PPV_ARGS(&m_rootSignature));
    if (FAILED(rootSignatureResult))
    {
        LogD3D12InfoQueueMessages(device, "root signature creation");
        WEST_HR_CHECK(rootSignatureResult);
    }
}

void DX12Pipeline::Initialize(ID3D12Device* device, const RHIGraphicsPipelineDesc& desc)
{
    WEST_ASSERT(device != nullptr);

    CreateRootSignature(device);
    m_primitiveTopology = ToD3D12PrimitiveTopology(desc.topology);

    // Build input layout from vertex attributes
    std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout;
    for (const auto& attr : desc.vertexAttributes)
    {
        D3D12_INPUT_ELEMENT_DESC element{};
        element.SemanticName = attr.semantic;
        element.SemanticIndex = 0;
        element.Format = static_cast<DXGI_FORMAT>(ToDXGIFormat(attr.format));
        element.InputSlot = 0;
        element.AlignedByteOffset = attr.offset;
        element.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        element.InstanceDataStepRate = 0;
        inputLayout.push_back(element);
    }

    // PSO description
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = m_rootSignature.Get();

    // Shader bytecodes
    psoDesc.VS.pShaderBytecode = desc.vertexShader.data();
    psoDesc.VS.BytecodeLength = desc.vertexShader.size();
    psoDesc.PS.pShaderBytecode = desc.fragmentShader.data();
    psoDesc.PS.BytecodeLength = desc.fragmentShader.size();

    // Input layout
    psoDesc.InputLayout.pInputElementDescs = inputLayout.data();
    psoDesc.InputLayout.NumElements = static_cast<UINT>(inputLayout.size());

    // Rasterizer state
    psoDesc.RasterizerState.FillMode =
        (desc.fillMode == RHIFillMode::Wireframe) ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode =
        (desc.cullMode == RHICullMode::None)  ? D3D12_CULL_MODE_NONE :
        (desc.cullMode == RHICullMode::Front) ? D3D12_CULL_MODE_FRONT :
                                                 D3D12_CULL_MODE_BACK;
    psoDesc.RasterizerState.FrontCounterClockwise = FALSE;
    psoDesc.RasterizerState.DepthBias = 0;
    psoDesc.RasterizerState.DepthBiasClamp = 0.0f;
    psoDesc.RasterizerState.SlopeScaledDepthBias = 0.0f;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;
    psoDesc.RasterizerState.MultisampleEnable = FALSE;
    psoDesc.RasterizerState.AntialiasedLineEnable = FALSE;

    // Blend state — default opaque
    psoDesc.BlendState.AlphaToCoverageEnable = FALSE;
    psoDesc.BlendState.IndependentBlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].BlendEnable = FALSE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // Depth stencil — disabled for Phase 2 triangle
    psoDesc.DepthStencilState.DepthEnable = desc.depthTest ? TRUE : FALSE;
    psoDesc.DepthStencilState.DepthWriteMask = desc.depthWrite ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
    psoDesc.DepthStencilState.DepthFunc = ToD3D12CompareOp(desc.depthCompare);
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.DepthStencilState.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    psoDesc.DepthStencilState.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;

    // Topology
    psoDesc.PrimitiveTopologyType = ToD3D12PrimitiveTopologyType(desc.topology);

    // Render targets
    psoDesc.NumRenderTargets = static_cast<UINT>(desc.colorFormats.size());
    for (uint32_t i = 0; i < desc.colorFormats.size() && i < 8; ++i)
    {
        psoDesc.RTVFormats[i] = static_cast<DXGI_FORMAT>(ToDXGIFormat(desc.colorFormats[i]));
    }

    if (desc.depthFormat != RHIFormat::Unknown)
    {
        psoDesc.DSVFormat = static_cast<DXGI_FORMAT>(ToDXGIFormat(desc.depthFormat));
    }

    // Sample
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleDesc.Quality = 0;

    const HRESULT psoResult = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso));
    if (FAILED(psoResult))
    {
        LogD3D12InfoQueueMessages(device, "graphics PSO creation");
        WEST_HR_CHECK(psoResult);
    }

    // Simple hash from shader sizes
    m_psoHash = desc.vertexShader.size() ^ (desc.fragmentShader.size() << 16);

    WEST_LOG_INFO(LogCategory::RHI, "DX12 Graphics Pipeline created: {}",
                  desc.debugName ? desc.debugName : "unnamed");
}

} // namespace west::rhi
