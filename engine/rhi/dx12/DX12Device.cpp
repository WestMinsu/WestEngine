// =============================================================================
// WestEngine - RHI DX12
// DX12 device implementation
// =============================================================================
#include "rhi/dx12/DX12Device.h"

#include "rhi/dx12/DX12Buffer.h"
#include "rhi/dx12/DX12CommandList.h"
#include "rhi/dx12/DX12Fence.h"
#include "rhi/dx12/DX12MemoryAllocator.h"
#include "rhi/dx12/DX12Pipeline.h"
#include "rhi/dx12/DX12Queue.h"
#include "rhi/dx12/DX12Sampler.h"
#include "rhi/dx12/DX12Semaphore.h"
#include "rhi/dx12/DX12SwapChain.h"
#include "rhi/dx12/DX12Texture.h"
#include "rhi/common/FormatConversion.h"
#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHIPipeline.h"
#include "rhi/interface/IRHISampler.h"
#include "rhi/interface/IRHITexture.h"

#include <d3d12sdklayers.h>
#include <algorithm>
#include <string>
#include <vector>

namespace west::rhi
{

namespace
{

void LogD3D12InfoQueueMessages(ID3D12Device* device, const char* context)
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

} // namespace

DX12Device::DX12Device() = default;

DX12Device::~DX12Device()
{
    // Ensure GPU is idle before destroying anything
    if (m_device)
    {
        WaitIdle();
    }

    // Flush all pending deletions before shutting down the allocator
    m_deletionQueue.FlushAll();

    // Destroy queues before device
    m_graphicsQueue.reset();
    m_computeQueue.reset();
    m_copyQueue.reset();

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

    bool debugLayerEnabled = false;
    if (config.enableDX12GPUBasedValidation && !config.enableValidation)
    {
        WEST_LOG_WARNING(LogCategory::RHI,
                         "DX12 GPU-Based Validation requested but validation is disabled; ignoring.");
    }
    if (config.enableValidation)
    {
        debugLayerEnabled = EnableDebugLayer(config.enableDX12GPUBasedValidation);
    }

    if (config.enableGPUCrashDiag)
    {
        EnableDRED();
    }

    // Create DXGI Factory
    UINT factoryFlags = 0;
    if (debugLayerEnabled)
    {
        factoryFlags = DXGI_CREATE_FACTORY_DEBUG;
    }

    WEST_HR_CHECK(CreateDXGIFactory2(factoryFlags, IID_PPV_ARGS(&m_factory)));

    SelectAdapter(config.preferredGPUIndex);
    CreateDevice();
    QueryDeviceCaps();
    WEST_CHECK(m_caps.maxBindlessResources > 0, "DX12 bindless requires ResourceBindingTier 2 or higher");
    CreateGlobalRootSignature();
    CreateBindlessHeaps();
    CreateQueues();

    // Initialize D3D12MA memory allocator
    m_memoryAllocator = std::make_unique<DX12MemoryAllocator>();
    if (!m_memoryAllocator->Initialize(m_device.Get(), m_adapter.Get()))
    {
        WEST_LOG_FATAL(LogCategory::RHI, "Failed to initialize D3D12MA");
        return false;
    }

    WEST_LOG_INFO(LogCategory::RHI, "DX12 Device initialized: {}", m_deviceName);
    WEST_LOG_INFO(LogCategory::RHI, "  VRAM: {} MB", m_caps.dedicatedVideoMemory / (1024 * 1024));
    WEST_LOG_INFO(LogCategory::RHI, "  Ray Tracing: {}", m_caps.supportsRayTracing ? "Yes" : "No");
    WEST_LOG_INFO(LogCategory::RHI, "  Mesh Shaders: {}", m_caps.supportsMeshShaders ? "Yes" : "No");
    WEST_LOG_INFO(LogCategory::RHI, "  Resizable BAR: {}", m_memoryAllocator->SupportsReBAR() ? "Yes" : "No");

    return true;
}

// ── Debug Layer ───────────────────────────────────────────────────────────

bool DX12Device::EnableDebugLayer(bool enableGBV)
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
        return true;
    }
    else
    {
        WEST_LOG_WARNING(LogCategory::RHI, "Failed to enable DX12 Debug Layer. Install Graphics Tools.");
        return false;
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
    uint32_t bestIndex = UINT32_MAX;
    bool preferredFound = false;
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

            if (adapterIndex == preferredIndex)
            {
                bestIndex = adapterIndex;
                preferredFound = true;
                bestVRAM = desc.DedicatedVideoMemory;
                break;
            }

            if (!preferredFound && (bestIndex == UINT32_MAX || desc.DedicatedVideoMemory > bestVRAM))
            {
                bestIndex = adapterIndex;
                bestVRAM = desc.DedicatedVideoMemory;
            }
        }

        adapterIndex++;
        adapter.Reset();
    }

    WEST_CHECK(bestIndex != UINT32_MAX, "No DX12-capable GPU found");

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

    D3D12_FEATURE_DATA_SHADER_MODEL shaderModel{};
    shaderModel.HighestShaderModel = D3D_SHADER_MODEL_6_6;
    const HRESULT shaderModelResult =
        m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &shaderModel, sizeof(shaderModel));
    WEST_CHECK(SUCCEEDED(shaderModelResult) && shaderModel.HighestShaderModel >= D3D_SHADER_MODEL_6_6,
               "DX12 bindless direct heap indexing requires Shader Model 6.6");

    // ReBAR support check
    D3D12_FEATURE_DATA_ARCHITECTURE1 arch{};
    arch.NodeIndex = 0;
    if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &arch, sizeof(arch))))
    {
        // Conservative Phase 2 signal for a fast CPU-visible GPU heap path.
        // SharedSystemMemory alone is not a reliable ReBAR indicator on discrete GPUs.
        m_caps.supportsResizableBar = arch.CacheCoherentUMA || arch.UMA;
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

void DX12Device::CreateGlobalRootSignature()
{
    static_assert(kMaxPushConstantSizeBytes % sizeof(uint32_t) == 0);

    D3D12_ROOT_PARAMETER1 rootParam{};
    rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    rootParam.Constants.ShaderRegister = 0;
    rootParam.Constants.RegisterSpace = 0;
    rootParam.Constants.Num32BitValues = kMaxPushConstantSizeBytes / sizeof(uint32_t);
    rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSigDesc{};
    rootSigDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootSigDesc.Desc_1_1.NumParameters = 1;
    rootSigDesc.Desc_1_1.pParameters = &rootParam;
    rootSigDesc.Desc_1_1.NumStaticSamplers = 0;
    rootSigDesc.Desc_1_1.pStaticSamplers = nullptr;
    rootSigDesc.Desc_1_1.Flags =
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
        D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
        D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED;

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    const HRESULT serializeResult = D3D12SerializeVersionedRootSignature(&rootSigDesc, &signature, &error);
    if (FAILED(serializeResult))
    {
        if (error)
        {
            WEST_LOG_ERROR(LogCategory::RHI, "Global root signature serialization failed: {}",
                           static_cast<const char*>(error->GetBufferPointer()));
        }
        WEST_HR_CHECK(serializeResult);
    }

    const HRESULT rootSignatureResult = m_device->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_globalRootSignature));
    if (FAILED(rootSignatureResult))
    {
        LogD3D12InfoQueueMessages(m_device.Get(), "global root signature creation");
        WEST_HR_CHECK(rootSignatureResult);
    }

    m_globalRootSignature->SetName(L"WestEngine Global Bindless Root Signature");
    WEST_LOG_INFO(LogCategory::RHI, "DX12 global bindless root signature created (max push constants={} bytes).",
                  kMaxPushConstantSizeBytes);
}

// ── Queue Creation ────────────────────────────────────────────────────────

void DX12Device::CreateQueues()
{
    m_graphicsQueue = std::make_unique<DX12Queue>();
    m_graphicsQueue->Initialize(m_device.Get(), RHIQueueType::Graphics);

    m_computeQueue = std::make_unique<DX12Queue>();
    m_computeQueue->Initialize(m_device.Get(), RHIQueueType::Compute);

    m_copyQueue = std::make_unique<DX12Queue>();
    m_copyQueue->Initialize(m_device.Get(), RHIQueueType::Copy);
}

void DX12Device::CreateBindlessHeaps()
{
    // One BindlessIndex namespace is shared by resources and samplers, so DX12 capacity must respect
    // the smaller shader-visible sampler heap limit.
    const uint32_t capacity = (std::min)(
        (std::min)(kBindlessCapacity, m_caps.maxBindlessResources),
        static_cast<uint32_t>(D3D12_MAX_SHADER_VISIBLE_SAMPLER_HEAP_SIZE));
    WEST_CHECK(capacity > 0, "DX12 bindless descriptor capacity is zero");

    D3D12_DESCRIPTOR_HEAP_DESC resourceHeapDesc{};
    resourceHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    resourceHeapDesc.NumDescriptors = capacity;
    resourceHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    WEST_HR_CHECK(m_device->CreateDescriptorHeap(&resourceHeapDesc, IID_PPV_ARGS(&m_resourceDescriptorHeap)));
    m_resourceDescriptorHeap->SetName(L"WestEngine Global Resource Descriptor Heap");

    D3D12_DESCRIPTOR_HEAP_DESC samplerHeapDesc{};
    samplerHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    samplerHeapDesc.NumDescriptors = capacity;
    samplerHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    WEST_HR_CHECK(m_device->CreateDescriptorHeap(&samplerHeapDesc, IID_PPV_ARGS(&m_samplerDescriptorHeap)));
    m_samplerDescriptorHeap->SetName(L"WestEngine Global Sampler Descriptor Heap");

    m_resourceDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_samplerDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    m_bindlessPool.Initialize(capacity);

    WEST_LOG_INFO(LogCategory::RHI, "DX12 bindless heaps created (capacity={}).", capacity);
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Device::GetResourceDescriptorCPU(BindlessIndex index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_resourceDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * m_resourceDescriptorSize;
    return handle;
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12Device::GetSamplerDescriptorCPU(BindlessIndex index) const
{
    D3D12_CPU_DESCRIPTOR_HANDLE handle = m_samplerDescriptorHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += static_cast<SIZE_T>(index) * m_samplerDescriptorSize;
    return handle;
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
    cmdList->Initialize(m_device.Get(), type, m_resourceDescriptorHeap.Get(), m_samplerDescriptorHeap.Get());
    return cmdList;
}

IRHIQueue* DX12Device::GetQueue(RHIQueueType type)
{
    switch (type)
    {
    case RHIQueueType::Graphics:
        return m_graphicsQueue.get();
    case RHIQueueType::Compute:
        return m_computeQueue.get();
    case RHIQueueType::Copy:
        return m_copyQueue.get();
    }

    WEST_ASSERT(false);
    return nullptr;
}

std::unique_ptr<IRHISwapChain> DX12Device::CreateSwapChain(const RHISwapChainDesc& desc)
{
    auto swapChain = std::make_unique<DX12SwapChain>();
    swapChain->Initialize(this, desc);
    return swapChain;
}

void DX12Device::WaitIdle()
{
    auto waitQueueIdle = [&](DX12Queue* queue)
    {
        if (!queue)
        {
            return;
        }

        ComPtr<ID3D12Fence> fence;
        WEST_HR_CHECK(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));

        HANDLE event = ::CreateEventW(nullptr, FALSE, FALSE, nullptr);
        WEST_ASSERT(event != nullptr);

        queue->GetD3DQueue()->Signal(fence.Get(), 1);

        fence->SetEventOnCompletion(1, event);
        ::WaitForSingleObject(event, INFINITE);
        ::CloseHandle(event);
    };

    waitQueueIdle(m_copyQueue.get());
    waitQueueIdle(m_computeQueue.get());
    waitQueueIdle(m_graphicsQueue.get());
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
    auto buffer = std::make_unique<DX12Buffer>();
    buffer->Initialize(this, desc);
    return buffer;
}

std::unique_ptr<IRHITexture> DX12Device::CreateTexture(const RHITextureDesc& desc)
{
    auto texture = std::make_unique<DX12Texture>();
    texture->Initialize(this, desc);
    return texture;
}

std::unique_ptr<IRHIBuffer> DX12Device::CreateTransientBuffer(const RHIBufferDesc& desc, uint32_t /*aliasSlot*/)
{
    return CreateBuffer(desc);
}

std::unique_ptr<IRHITexture> DX12Device::CreateTransientTexture(const RHITextureDesc& desc, uint32_t /*aliasSlot*/)
{
    return CreateTexture(desc);
}

std::unique_ptr<IRHISampler> DX12Device::CreateSampler(const RHISamplerDesc& desc)
{
    return std::make_unique<DX12Sampler>(desc);
}

std::unique_ptr<IRHIPipeline> DX12Device::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc)
{
    auto pipeline = std::make_unique<DX12Pipeline>();
    pipeline->Initialize(m_device.Get(), m_globalRootSignature.Get(), desc);
    return pipeline;
}

std::unique_ptr<IRHIPipeline> DX12Device::CreateComputePipeline(const RHIComputePipelineDesc& desc)
{
    auto pipeline = std::make_unique<DX12Pipeline>();
    pipeline->Initialize(m_device.Get(), m_globalRootSignature.Get(), desc);
    return pipeline;
}

BindlessIndex DX12Device::RegisterBindlessResource(IRHIBuffer* buffer)
{
    WEST_ASSERT(buffer != nullptr);

    auto* dx12Buffer = static_cast<DX12Buffer*>(buffer);
    std::lock_guard lock(m_bindlessMutex);

    BindlessIndex index = m_bindlessPool.Allocate();
    if (index == kInvalidBindlessIndex)
    {
        WEST_LOG_ERROR(LogCategory::RHI, "DX12 bindless heap exhausted while registering buffer");
        return kInvalidBindlessIndex;
    }

    const RHIBufferDesc& desc = buffer->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;

    if (desc.structureByteStride > 0)
    {
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.Buffer.NumElements = static_cast<UINT>(desc.sizeBytes / desc.structureByteStride);
        srvDesc.Buffer.StructureByteStride = desc.structureByteStride;
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    }
    else
    {
        srvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
        srvDesc.Buffer.NumElements = static_cast<UINT>(desc.sizeBytes / sizeof(uint32_t));
        srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
    }

    m_device->CreateShaderResourceView(dx12Buffer->GetD3DResource(), &srvDesc, GetResourceDescriptorCPU(index));
    dx12Buffer->SetBindlessIndex(index);
    return index;
}

BindlessIndex DX12Device::RegisterBindlessResource(IRHITexture* texture)
{
    WEST_ASSERT(texture != nullptr);

    auto* dx12Texture = static_cast<DX12Texture*>(texture);
    std::lock_guard lock(m_bindlessMutex);

    BindlessIndex index = m_bindlessPool.Allocate();
    if (index == kInvalidBindlessIndex)
    {
        WEST_LOG_ERROR(LogCategory::RHI, "DX12 bindless heap exhausted while registering texture");
        return kInvalidBindlessIndex;
    }

    const RHITextureDesc& desc = texture->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = static_cast<DXGI_FORMAT>(ToDXGIFormat(desc.format));
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels = desc.mipLevels;
    srvDesc.Texture2D.PlaneSlice = 0;
    srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

    m_device->CreateShaderResourceView(dx12Texture->GetD3DResource(), &srvDesc, GetResourceDescriptorCPU(index));
    dx12Texture->SetBindlessIndex(index);
    return index;
}

static D3D12_FILTER_TYPE ToD3D12FilterType(RHIFilter filter)
{
    return filter == RHIFilter::Nearest ? D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;
}

static D3D12_FILTER_TYPE ToD3D12MipFilterType(RHIMipmapMode mode)
{
    return mode == RHIMipmapMode::Nearest ? D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;
}

static D3D12_FILTER ToD3D12Filter(const RHISamplerDesc& desc)
{
    if (desc.anisotropyEnable)
    {
        return desc.compareEnable ? D3D12_FILTER_COMPARISON_ANISOTROPIC : D3D12_FILTER_ANISOTROPIC;
    }

    const D3D12_FILTER_REDUCTION_TYPE reduction =
        desc.compareEnable ? D3D12_FILTER_REDUCTION_TYPE_COMPARISON : D3D12_FILTER_REDUCTION_TYPE_STANDARD;
    return D3D12_ENCODE_BASIC_FILTER(ToD3D12FilterType(desc.minFilter),
                                     ToD3D12FilterType(desc.magFilter),
                                     ToD3D12MipFilterType(desc.mipmapMode),
                                     reduction);
}

static D3D12_TEXTURE_ADDRESS_MODE ToD3D12AddressMode(RHIAddressMode mode)
{
    switch (mode)
    {
    case RHIAddressMode::Repeat:
        return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
    case RHIAddressMode::MirroredRepeat:
        return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
    case RHIAddressMode::ClampToBorder:
        return D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    case RHIAddressMode::ClampToEdge:
    default:
        return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    }
}

static D3D12_COMPARISON_FUNC ToD3D12Compare(RHICompareOp op)
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

BindlessIndex DX12Device::RegisterBindlessResource(IRHISampler* sampler)
{
    WEST_ASSERT(sampler != nullptr);

    auto* dx12Sampler = static_cast<DX12Sampler*>(sampler);
    std::lock_guard lock(m_bindlessMutex);

    BindlessIndex index = m_bindlessPool.Allocate();
    if (index == kInvalidBindlessIndex)
    {
        WEST_LOG_ERROR(LogCategory::RHI, "DX12 bindless heap exhausted while registering sampler");
        return kInvalidBindlessIndex;
    }

    const RHISamplerDesc& desc = sampler->GetDesc();
    D3D12_SAMPLER_DESC samplerDesc{};
    samplerDesc.Filter = ToD3D12Filter(desc);
    samplerDesc.AddressU = ToD3D12AddressMode(desc.addressU);
    samplerDesc.AddressV = ToD3D12AddressMode(desc.addressV);
    samplerDesc.AddressW = ToD3D12AddressMode(desc.addressW);
    samplerDesc.MipLODBias = desc.mipLodBias;
    samplerDesc.MaxAnisotropy = desc.anisotropyEnable ? static_cast<UINT>(desc.maxAnisotropy) : 1;
    samplerDesc.ComparisonFunc = desc.compareEnable ? ToD3D12Compare(desc.compareOp) : D3D12_COMPARISON_FUNC_NEVER;
    samplerDesc.MinLOD = desc.minLod;
    samplerDesc.MaxLOD = desc.maxLod;

    if (desc.borderColor == RHIBorderColor::OpaqueWhite)
    {
        samplerDesc.BorderColor[0] = 1.0f;
        samplerDesc.BorderColor[1] = 1.0f;
        samplerDesc.BorderColor[2] = 1.0f;
        samplerDesc.BorderColor[3] = 1.0f;
    }
    else if (desc.borderColor == RHIBorderColor::OpaqueBlack)
    {
        samplerDesc.BorderColor[3] = 1.0f;
    }

    m_device->CreateSampler(&samplerDesc, GetSamplerDescriptorCPU(index));
    dx12Sampler->SetBindlessIndex(index);
    return index;
}

void DX12Device::UnregisterBindlessResource(BindlessIndex index)
{
    std::lock_guard lock(m_bindlessMutex);
    if (!m_bindlessPool.Free(index))
    {
        WEST_LOG_WARNING(LogCategory::RHI, "DX12 bindless unregister ignored for invalid index {}", index);
    }
}

} // namespace west::rhi
