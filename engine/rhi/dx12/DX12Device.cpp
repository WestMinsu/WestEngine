// =============================================================================
// WestEngine - RHI DX12
// DX12 device implementation
// =============================================================================
#include "rhi/dx12/DX12Device.h"

#include "rhi/dx12/DX12CommandList.h"
#include "rhi/dx12/DX12Fence.h"
#include "rhi/dx12/DX12Queue.h"
#include "rhi/dx12/DX12Semaphore.h"
#include "rhi/dx12/DX12SwapChain.h"
#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHIPipeline.h"
#include "rhi/interface/IRHISampler.h"
#include "rhi/interface/IRHITexture.h"

#include <string>

namespace west::rhi
{

DX12Device::~DX12Device()
{
    // Ensure GPU is idle before destroying anything
    if (m_device)
    {
        WaitIdle();
    }

    // Destroy queues before device
    m_graphicsQueue.reset();

    // Check for device removed before final release
    if (m_device && m_dredEnabled)
    {
        HRESULT reason = m_device->GetDeviceRemovedReason();
        if (FAILED(reason))
        {
            WEST_LOG_ERROR(LogCategory::RHI, "Device was removed during shutdown. Reason: 0x{:08X}",
                           static_cast<uint32_t>(reason));

            // Query DRED data
            ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
            if (SUCCEEDED(m_device->QueryInterface(IID_PPV_ARGS(&dred))))
            {
                D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs{};
                if (SUCCEEDED(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs)))
                {
                    WEST_LOG_ERROR(LogCategory::RHI, "DRED Auto-Breadcrumbs available");
                }

                D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault{};
                if (SUCCEEDED(dred->GetPageFaultAllocationOutput1(&pageFault)))
                {
                    WEST_LOG_ERROR(LogCategory::RHI, "DRED Page Fault at VA: 0x{:016X}", pageFault.PageFaultVA);
                }
            }
        }
    }

    WEST_LOG_INFO(LogCategory::RHI, "DX12 Device destroyed.");
}

bool DX12Device::Initialize(const RHIDeviceConfig& config)
{
    WEST_LOG_INFO(LogCategory::RHI, "Initializing DX12 Device...");

    if (config.enableValidation)
    {
        EnableDebugLayer(false);
    }

    if (config.enableGPUCrashDiag)
    {
        EnableDRED();
    }

    // Create DXGI Factory
    UINT factoryFlags = 0;
    if (config.enableValidation)
    {
        factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
    }

    WEST_HR_CHECK(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)));

    SelectAdapter(config.preferredGPUIndex);
    CreateDevice();
    QueryDeviceCaps();
    CreateQueues();

    WEST_LOG_INFO(LogCategory::RHI, "DX12 Device initialized: {}", m_deviceName);
    WEST_LOG_INFO(LogCategory::RHI, "  VRAM: {} MB", m_caps.dedicatedVideoMemory / (1024 * 1024));
    WEST_LOG_INFO(LogCategory::RHI, "  Ray Tracing: {}", m_caps.supportsRayTracing ? "Yes" : "No");
    WEST_LOG_INFO(LogCategory::RHI, "  Mesh Shaders: {}", m_caps.supportsMeshShaders ? "Yes" : "No");

    return true;
}

// ── Debug Layer ───────────────────────────────────────────────────────────

void DX12Device::EnableDebugLayer(bool enableGBV)
{
    ComPtr<ID3D12Debug5> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
    {
        debugController->EnableDebugLayer();
        WEST_LOG_INFO(LogCategory::RHI, "DX12 Debug Layer enabled.");

        if (enableGBV)
        {
            debugController->SetEnableGPUBasedValidation(TRUE);
            WEST_LOG_INFO(LogCategory::RHI, "DX12 GPU-Based Validation enabled.");
        }
    }
    else
    {
        WEST_LOG_WARNING(LogCategory::RHI, "Failed to enable DX12 Debug Layer. Install Graphics Tools.");
    }
}

void DX12Device::EnableDRED()
{
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings))))
    {
        dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        dredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
        m_dredEnabled = true;
        WEST_LOG_INFO(LogCategory::RHI, "DRED (Device Removed Extended Data) enabled.");
    }
    else
    {
        WEST_LOG_WARNING(LogCategory::RHI, "Failed to enable DRED. Requires Windows 10 20H1+.");
    }
}

// ── Adapter Selection ─────────────────────────────────────────────────────

void DX12Device::SelectAdapter(uint32_t preferredIndex)
{
    ComPtr<IDXGIAdapter4> adapter;
    uint32_t adapterIndex = 0;
    uint32_t bestIndex = 0;
    uint64_t bestVRAM = 0;

    // Enumerate adapters, prefer high-performance GPU
    while (m_factory->EnumAdapterByGpuPreference(adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                 IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND)
    {
        DXGI_ADAPTER_DESC3 desc{};
        adapter->GetDesc3(&desc);

        // Skip software adapters
        if (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)
        {
            adapterIndex++;
            adapter.Reset();
            continue;
        }

        // Check if D3D12 is supported
        if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device), nullptr)))
        {
            // Convert adapter name to narrow string for logging
            char narrowName[128]{};
            size_t converted = 0;
            wcstombs_s(&converted, narrowName, desc.Description, sizeof(narrowName) - 1);

            WEST_LOG_INFO(LogCategory::RHI, "  GPU[{}]: {} ({} MB VRAM)", adapterIndex, narrowName,
                          desc.DedicatedVideoMemory / (1024 * 1024));

            if (adapterIndex == preferredIndex || desc.DedicatedVideoMemory > bestVRAM)
            {
                if (adapterIndex == preferredIndex || bestVRAM == 0)
                {
                    bestIndex = adapterIndex;
                    bestVRAM = desc.DedicatedVideoMemory;
                }
            }
        }

        adapterIndex++;
        adapter.Reset();
    }

    // Re-acquire the selected adapter
    WEST_HR_CHECK(m_factory->EnumAdapterByGpuPreference(bestIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                        IID_PPV_ARGS(&m_adapter)));

    m_adapter->GetDesc3(&m_adapterDesc);

    // Store device name
    char narrowName[128]{};
    size_t converted = 0;
    wcstombs_s(&converted, narrowName, m_adapterDesc.Description, sizeof(narrowName) - 1);
    m_deviceName = narrowName;
}

// ── Device Creation ───────────────────────────────────────────────────────

void DX12Device::CreateDevice()
{
    WEST_HR_CHECK(D3D12CreateDevice(m_adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&m_device)));

    // Set stable power state for profiling (debug only)
#if WEST_DEBUG
    m_device->SetStablePowerState(TRUE);
#endif

    // Name the device for PIX/debugger
    m_device->SetName(L"WestEngine DX12 Device");
}

void DX12Device::QueryDeviceCaps()
{
    m_caps.dedicatedVideoMemory = m_adapterDesc.DedicatedVideoMemory;

    // Ray Tracing support
    D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
    {
        m_caps.supportsRayTracing = (options5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_1);
    }

    // Mesh Shader support
    D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7{};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7, &options7, sizeof(options7))))
    {
        m_caps.supportsMeshShaders = (options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1);
    }

    // ReBAR support check
    D3D12_FEATURE_DATA_ARCHITECTURE1 arch{};
    arch.NodeIndex = 0;
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &arch, sizeof(arch))))
    {
        // ReBAR is indicated by large BAR allocation being possible
        // We approximate by checking shared memory size
        m_caps.supportsResizableBar = (m_adapterDesc.SharedSystemMemory > 256ull * 1024 * 1024);
    }

    // Bindless: DX12 Tier 2+ supports unbounded descriptor arrays
    D3D12_FEATURE_DATA_D3D12_OPTIONS options{};
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
    {
        if (options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_2)
        {
            m_caps.maxBindlessResources = 1000000; // Effectively unbounded
        }
    }
}

// ── Queue Creation ────────────────────────────────────────────────────────

void DX12Device::CreateQueues()
{
    m_graphicsQueue = std::make_unique<DX12Queue>();
    m_graphicsQueue->Initialize(m_device.Get(), RHIQueueType::Graphics);
}

// ── IRHIDevice Implementation ─────────────────────────────────────────────

std::unique_ptr<IRHIFence> DX12Device::CreateFence(uint64_t initialValue)
{
    auto fence = std::make_unique<DX12Fence>();
    fence->Initialize(m_device.Get(), initialValue);
    return fence;
}

std::unique_ptr<IRHISemaphore> DX12Device::CreateBinarySemaphore()
{
    // DX12 doesn't need binary semaphores — Fence handles everything
    return std::make_unique<DX12Semaphore>();
}

std::unique_ptr<IRHICommandList> DX12Device::CreateCommandList(RHIQueueType type)
{
    auto cmdList = std::make_unique<DX12CommandList>();
    cmdList->Initialize(m_device.Get(), type);
    return cmdList;
}

IRHIQueue* DX12Device::GetQueue(RHIQueueType type)
{
    // Phase 1: only Graphics queue
    WEST_ASSERT(type == RHIQueueType::Graphics);
    return m_graphicsQueue.get();
}

std::unique_ptr<IRHISwapChain> DX12Device::CreateSwapChain(const RHISwapChainDesc& desc)
{
    auto swapChain = std::make_unique<DX12SwapChain>();
    swapChain->Initialize(this, desc);
    return swapChain;
}

void DX12Device::WaitIdle()
{
    if (m_graphicsQueue)
    {
        // Create a temporary fence to flush the queue
        ComPtr<ID3D12Fence> fence;
        WEST_HR_CHECK(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

        HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        WEST_ASSERT(event != nullptr);

        auto* queue = static_cast<DX12Queue*>(m_graphicsQueue.get());
        queue->GetD3DQueue()->Signal(fence.Get(), 1);

        fence->SetEventOnCompletion(1, event);
        ::WaitForSingleObject(event, INFINITE);
        ::CloseHandle(event);
    }
}

const char* DX12Device::GetDeviceName() const
{
    return m_deviceName.c_str();
}

RHIDeviceCaps DX12Device::GetCapabilities() const
{
    return m_caps;
}

// ── Stub implementations (Phase 2+) ──────────────────────────────────────

std::unique_ptr<IRHIBuffer> DX12Device::CreateBuffer(const RHIBufferDesc& desc)
{
    // TODO(minsu): Phase 2 — D3D12MA buffer allocation
    WEST_LOG_WARNING(LogCategory::RHI, "DX12Device::CreateBuffer — stub, not yet implemented");
    return nullptr;
}

std::unique_ptr<IRHITexture> DX12Device::CreateTexture(const RHITextureDesc& desc)
{
    // TODO(minsu): Phase 2 — D3D12MA texture allocation
    WEST_LOG_WARNING(LogCategory::RHI, "DX12Device::CreateTexture — stub, not yet implemented");
    return nullptr;
}

std::unique_ptr<IRHISampler> DX12Device::CreateSampler(const RHISamplerDesc& desc)
{
    // TODO(minsu): Phase 3 — Sampler heap allocation
    WEST_LOG_WARNING(LogCategory::RHI, "DX12Device::CreateSampler — stub, not yet implemented");
    return nullptr;
}

std::unique_ptr<IRHIPipeline> DX12Device::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc)
{
    // TODO(minsu): Phase 4 — PSO creation with Slang bytecode
    WEST_LOG_WARNING(LogCategory::RHI, "DX12Device::CreateGraphicsPipeline — stub");
    return nullptr;
}

std::unique_ptr<IRHIPipeline> DX12Device::CreateComputePipeline(const RHIComputePipelineDesc& desc)
{
    // TODO(minsu): Phase 4 — Compute PSO
    WEST_LOG_WARNING(LogCategory::RHI, "DX12Device::CreateComputePipeline — stub");
    return nullptr;
}

BindlessIndex DX12Device::RegisterBindlessResource(IRHIBuffer* buffer)
{
    // TODO(minsu): Phase 3 — Bindless descriptor heap registration
    return kInvalidBindlessIndex;
}

BindlessIndex DX12Device::RegisterBindlessResource(IRHITexture* texture)
{
    // TODO(minsu): Phase 3 — Bindless descriptor heap registration
    return kInvalidBindlessIndex;
}

void DX12Device::UnregisterBindlessResource(BindlessIndex index)
{
    // TODO(minsu): Phase 3 — Bindless descriptor heap unregistration
}

} // namespace west::rhi
