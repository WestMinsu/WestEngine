// =============================================================================
// WestEngine - RHI DX12
// DX12 pipeline implementation — Phase 2 minimal PSO
// =============================================================================
#include "rhi/dx12/DX12Pipeline.h"

#include "rhi/common/FormatConversion.h"
#include "rhi/interface/RHIDescriptors.h"

#include <vector>

namespace west::rhi
{

void DX12Pipeline::CreateRootSignature(ID3D12Device* device)
{
    // Phase 2: Empty root signature (no descriptors, no push constants)
    // Phase 3+: Global bindless root signature with push constants
    D3D12_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.NumParameters = 0;
    rootSigDesc.pParameters = nullptr;
    rootSigDesc.NumStaticSamplers = 0;
    rootSigDesc.pStaticSamplers = nullptr;
    rootSigDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                                              &signature, &error);
    if (FAILED(hr))
    {
        if (error)
        {
            WEST_LOG_ERROR(LogCategory::RHI, "Root Signature serialization failed: {}",
                           static_cast<const char*>(error->GetBufferPointer()));
        }
        WEST_HR_CHECK(hr);
    }

    WEST_HR_CHECK(device->CreateRootSignature(0, signature->GetBufferPointer(),
                                               signature->GetBufferSize(),
                                               IID_PPV_ARGS(&m_rootSignature)));
}

void DX12Pipeline::Initialize(ID3D12Device* device, const RHIGraphicsPipelineDesc& desc)
{
    WEST_ASSERT(device != nullptr);

    CreateRootSignature(device);

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
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    // Topology
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

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

    WEST_HR_CHECK(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pso)));

    // Simple hash from shader sizes
    m_psoHash = desc.vertexShader.size() ^ (desc.fragmentShader.size() << 16);

    WEST_LOG_INFO(LogCategory::RHI, "DX12 Graphics Pipeline created: {}",
                  desc.debugName ? desc.debugName : "unnamed");
}

} // namespace west::rhi
