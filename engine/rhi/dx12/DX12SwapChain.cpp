// =============================================================================
// WestEngine - RHI DX12
// DX12 swap chain implementation — Flip Model + tearing support
// =============================================================================
#include "rhi/dx12/DX12SwapChain.h"

#include "rhi/dx12/DX12Device.h"
#include "rhi/dx12/DX12Queue.h"

namespace west::rhi
{

DX12SwapChain::~DX12SwapChain()
{
    ReleaseBackBuffers();
    WEST_LOG_INFO(LogCategory::RHI, "DX12 SwapChain destroyed.");
}

void DX12SwapChain::Initialize(DX12Device* device, const RHISwapChainDesc& desc)
{
    m_device = device;
    m_bufferCount = desc.bufferCount;
    m_format = desc.format;
    m_vsync = desc.vsync;

    auto* factory = device->GetDXGIFactory();

    // Check tearing support (VRR / FreeSync / G-Sync)
    {
        ComPtr<IDXGIFactory5> factory5;
        if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5))))
        {
            BOOL allowTearing = FALSE;
            if (SUCCEEDED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing,
                                                        sizeof(allowTearing))))
            {
                m_tearingSupport = (allowTearing == TRUE);
            }
        }
    }

    WEST_LOG_INFO(LogCategory::RHI, "Tearing support: {}", m_tearingSupport ? "Yes" : "No");

    // Convert RHIFormat to DXGI_FORMAT
    // Phase 1: only support RGBA8_UNORM and BGRA8_UNORM
    DXGI_FORMAT dxgiFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (desc.format == RHIFormat::BGRA8_UNORM)
    {
        dxgiFormat = DXGI_FORMAT_B8G8R8A8_UNORM;
    }

    // Create swap chain using CreateSwapChainForHwnd (modern path)
    DXGI_SWAP_CHAIN_DESC1 swapChainDesc{};
    swapChainDesc.Width = desc.width;
    swapChainDesc.Height = desc.height;
    swapChainDesc.Format = dxgiFormat;
    swapChainDesc.Stereo = FALSE;
    swapChainDesc.SampleDesc = {1, 0};
    swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapChainDesc.BufferCount = desc.bufferCount;
    swapChainDesc.Scaling = DXGI_SCALING_STRETCH;
    swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapChainDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapChainDesc.Flags = m_tearingSupport ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    auto* graphicsQueue = static_cast<DX12Queue*>(device->GetQueue(RHIQueueType::Graphics));

    ComPtr<IDXGISwapChain1> swapChain1;
    WEST_HR_CHECK(factory->CreateSwapChainForHwnd(graphicsQueue->GetD3DQueue(), static_cast<HWND>(desc.windowHandle),
                                                  &swapChainDesc,
                                                  nullptr, // No fullscreen desc
                                                  nullptr, // No restrict output
                                                  &swapChain1));

    // Disable Alt+Enter fullscreen toggle — we manage this ourselves
    WEST_HR_CHECK(factory->MakeWindowAssociation(static_cast<HWND>(desc.windowHandle), DXGI_MWA_NO_ALT_ENTER));

    WEST_HR_CHECK(swapChain1.As(&m_swapChain));

    CreateRTVHeap();
    AcquireBackBuffers();

    m_currentIndex = m_swapChain->GetCurrentBackBufferIndex();

    WEST_LOG_INFO(LogCategory::RHI, "DX12 SwapChain created: {}x{}, {} buffers, format: {}", desc.width, desc.height,
                  desc.bufferCount, (desc.format == RHIFormat::BGRA8_UNORM) ? "BGRA8_UNORM" : "RGBA8_UNORM");
}

void DX12SwapChain::CreateRTVHeap()
{
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{};
    rtvHeapDesc.NumDescriptors = m_bufferCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

    WEST_HR_CHECK(m_device->GetD3DDevice()->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

    m_rtvDescriptorSize = m_device->GetD3DDevice()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
}

void DX12SwapChain::AcquireBackBuffers()
{
    m_backBufferResources.resize(m_bufferCount);
    m_backBufferTextures.resize(m_bufferCount);

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = m_rtvHeap->GetCPUDescriptorHandleForHeapStart();

    for (uint32_t i = 0; i < m_bufferCount; ++i)
    {
        WEST_HR_CHECK(m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBufferResources[i])));

        // Create RTV for this back buffer
        m_device->GetD3DDevice()->CreateRenderTargetView(m_backBufferResources[i].Get(), nullptr, rtvHandle);

        // Name for PIX
        wchar_t name[32];
        swprintf_s(name, L"BackBuffer[%u]", i);
        m_backBufferResources[i]->SetName(name);

        // Initialize the texture wrapper
        RHITextureDesc texDesc{};
        DXGI_SWAP_CHAIN_DESC1 scDesc{};
        m_swapChain->GetDesc1(&scDesc);
        texDesc.width = scDesc.Width;
        texDesc.height = scDesc.Height;
        texDesc.format = m_format;
        texDesc.usage = RHITextureUsage::RenderTarget | RHITextureUsage::Present;

        m_backBufferTextures[i].InitFromExisting(m_backBufferResources[i].Get(), texDesc, rtvHandle);

        rtvHandle.ptr += m_rtvDescriptorSize;
    }
}

void DX12SwapChain::ReleaseBackBuffers()
{
    m_backBufferTextures.clear();
    m_backBufferResources.clear();
}

// ── IRHISwapChain interface ───────────────────────────────────────────────

uint32_t DX12SwapChain::AcquireNextImage(IRHISemaphore* /*acquireSemaphore*/)
{
    // DX12: DXGI manages acquire internally
    // The current back buffer index is updated after Present()
    m_currentIndex = m_swapChain->GetCurrentBackBufferIndex();
    return m_currentIndex;
}

bool DX12SwapChain::Present(IRHISemaphore* /*presentSemaphore*/)
{
    UINT syncInterval = m_vsync ? 1 : 0;
    UINT presentFlags = 0;

    if (!m_vsync && m_tearingSupport)
    {
        presentFlags = DXGI_PRESENT_ALLOW_TEARING;
    }

    HRESULT hr = m_swapChain->Present(syncInterval, presentFlags);

    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET)
    {
        WEST_LOG_FATAL(LogCategory::RHI, "Device removed during Present! HRESULT: 0x{:08X}", static_cast<uint32_t>(hr));
        // DRED data will be queried in DX12Device destructor
        return false;
    }

    WEST_HR_CHECK(hr);
    return true;
}

IRHITexture* DX12SwapChain::GetCurrentBackBuffer()
{
    return &m_backBufferTextures[m_currentIndex];
}

void DX12SwapChain::Resize(uint32_t width, uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return; // Minimized window
    }

    ReleaseBackBuffers();

    DXGI_SWAP_CHAIN_DESC1 desc{};
    m_swapChain->GetDesc1(&desc);

    WEST_HR_CHECK(m_swapChain->ResizeBuffers(m_bufferCount, width, height, desc.Format, desc.Flags));

    AcquireBackBuffers();
    m_currentIndex = m_swapChain->GetCurrentBackBufferIndex();

    WEST_LOG_INFO(LogCategory::RHI, "DX12 SwapChain resized: {}x{}", width, height);
}

} // namespace west::rhi
