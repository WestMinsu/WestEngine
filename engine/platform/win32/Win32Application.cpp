// =============================================================================
// Platform (Win32)
// Win32 application lifecycle with RHI ClearColor rendering loop
// =============================================================================
#include "platform/win32/Win32Application.h"

#include "core/Assert.h"
#include "core/Logger.h"
#include "core/Profiler.h"
#include "core/Threading/TaskSystem.h"
#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHIFence.h"
#include "rhi/interface/IRHIPipeline.h"
#include "rhi/interface/IRHIQueue.h"
#include "rhi/interface/IRHISampler.h"
#include "rhi/interface/IRHISemaphore.h"
#include "rhi/interface/IRHISwapChain.h"
#include "rhi/interface/IRHITexture.h"
#include "rhi/interface/RHIDescriptors.h"
#include "rhi/interface/RHIFactory.h"
#include "rhi/common/TexturedQuadShaderData.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <format>
#include <string_view>
#include <system_error>
#include <thread>

namespace west
{

namespace
{

const char* BackendName(rhi::RHIBackend backend)
{
    return (backend == rhi::RHIBackend::DX12) ? "DX12" : "Vulkan";
}

const char* OnOff(bool value)
{
    return value ? "on" : "off";
}

const char* BuildConfigName()
{
#if WEST_DEBUG
    return "Debug";
#else
    return "Release";
#endif
}

} // namespace

bool Win32Application::Initialize()
{
    WEST_PROFILE_FUNCTION();
    Logger::Initialize();

    WEST_LOG_INFO(LogCategory::Core, "WestEngine v0.1.0 initializing...");

#if WEST_DEBUG
    m_enableValidation = true;
    m_enableGPUCrashDiag = true;
#else
    m_enableValidation = false;
    m_enableGPUCrashDiag = false;
#endif

    // Create main window
    m_window = std::make_unique<Win32Window>();

    WindowDesc windowDesc;
    windowDesc.title = "WestEngine";
    windowDesc.width = 1920;
    windowDesc.height = 1080;

    if (!m_window->Create(windowDesc))
    {
        WEST_LOG_FATAL(LogCategory::Platform, "Failed to create main window");
        return false;
    }

    // Parse backend from command line
    // Simple check: if any argument contains "vulkan", use Vulkan backend
    int argc = __argc;
    char** argv = __argv;
    for (int i = 1; i < argc; ++i)
    {
        const std::string_view arg(argv[i]);
        if (arg.find("vulkan") != std::string_view::npos)
        {
            m_backend = rhi::RHIBackend::Vulkan;
            WEST_LOG_INFO(LogCategory::Core, "Vulkan backend selected via command line.");
        }

        if (arg == "--validation")
        {
            m_enableValidation = true;
        }
        else if (arg == "--no-validation")
        {
            m_enableValidation = false;
        }
        else if (arg == "--dx12-gbv")
        {
            m_enableDX12GPUBasedValidation = true;
        }
        else if (arg == "--no-dx12-gbv")
        {
            m_enableDX12GPUBasedValidation = false;
        }
        else if (arg == "--gpu-crash-diag")
        {
            m_enableGPUCrashDiag = true;
        }
        else if (arg == "--no-gpu-crash-diag")
        {
            m_enableGPUCrashDiag = false;
        }

        if (arg == "--smoke-test")
        {
            m_maxFrameCount = 3;
            WEST_LOG_INFO(LogCategory::Core, "Smoke-test mode enabled ({} frames).", m_maxFrameCount);
        }

        static constexpr std::string_view kFramesPrefix = "--frames=";
        if (arg.starts_with(kFramesPrefix))
        {
            const std::string_view frameText = arg.substr(kFramesPrefix.size());
            uint32 parsedFrameCount = 0;
            const char* begin = frameText.data();
            const char* end = begin + frameText.size();
            const auto [ptr, ec] = std::from_chars(begin, end, parsedFrameCount);
            if (ec == std::errc{} && ptr == end && parsedFrameCount > 0)
            {
                m_maxFrameCount = parsedFrameCount;
                WEST_LOG_INFO(LogCategory::Core, "Frame limit set to {}.", m_maxFrameCount);
            }
            else
            {
                WEST_LOG_WARNING(LogCategory::Core, "Ignoring invalid frame limit argument: {}", arg);
            }
        }
    }

    if (m_enableDX12GPUBasedValidation && !m_enableValidation)
    {
        WEST_LOG_WARNING(LogCategory::Core, "Ignoring --dx12-gbv because validation is disabled.");
        m_enableDX12GPUBasedValidation = false;
    }
    if (m_enableDX12GPUBasedValidation && m_backend != rhi::RHIBackend::DX12)
    {
        WEST_LOG_WARNING(LogCategory::Core, "Ignoring --dx12-gbv because the active backend is not DX12.");
        m_enableDX12GPUBasedValidation = false;
    }

    Logger::Log(LogLevel::Info, LogCategory::Core,
                std::format("Launch config: build={}, backend={}, validation={}, dx12GBV={}, gpuCrashDiag={}, "
                            "frameLimit={}",
                            BuildConfigName(), BackendName(m_backend), OnOff(m_enableValidation),
                            OnOff(m_enableDX12GPUBasedValidation), OnOff(m_enableGPUCrashDiag), m_maxFrameCount));

    InitializeRHI();

    m_timer.Reset();
    m_isRunning = true;

    WEST_LOG_INFO(LogCategory::Core, "Initialization complete.");
    return true;
}

void Win32Application::InitializeRHI()
{
    WEST_PROFILE_FUNCTION();

    rhi::RHIDeviceConfig config{};
    config.enableValidation = m_enableValidation;
    config.enableDX12GPUBasedValidation = m_enableDX12GPUBasedValidation;
    config.enableGPUCrashDiag = m_enableGPUCrashDiag;
    config.preferredGPUIndex = UINT32_MAX;
    config.windowHandle = m_window->GetNativeHandle();
    config.windowWidth = m_window->GetWidth();
    config.windowHeight = m_window->GetHeight();

    // Create RHI device
    m_rhiDevice = rhi::RHIFactory::CreateDevice(m_backend, config);
    WEST_CHECK(m_rhiDevice != nullptr, "Failed to create RHI device");

    if (m_backend == rhi::RHIBackend::DX12)
    {
        Logger::Log(LogLevel::Info, LogCategory::RHI,
                    std::format("RHI Backend: {} (validation={}, dx12GBV={}, gpuCrashDiag={})",
                                BackendName(m_backend), OnOff(config.enableValidation),
                                OnOff(config.enableDX12GPUBasedValidation), OnOff(config.enableGPUCrashDiag)));
    }
    else
    {
        Logger::Log(LogLevel::Info, LogCategory::RHI,
                    std::format("RHI Backend: {} (validation={}, gpuCrashDiag={})", BackendName(m_backend),
                                OnOff(config.enableValidation), OnOff(config.enableGPUCrashDiag)));
    }
    Logger::Log(LogLevel::Info, LogCategory::RHI, std::format("GPU: {}", m_rhiDevice->GetDeviceName()));

    // Create swap chain
    rhi::RHISwapChainDesc swapChainDesc{};
    swapChainDesc.windowHandle = m_window->GetNativeHandle();
    swapChainDesc.width = m_window->GetWidth();
    swapChainDesc.height = m_window->GetHeight();
    swapChainDesc.format = rhi::RHIFormat::BGRA8_UNORM;
    swapChainDesc.bufferCount = 3;
    swapChainDesc.vsync = false;

    m_swapChain = m_rhiDevice->CreateSwapChain(swapChainDesc);
    WEST_CHECK(m_swapChain != nullptr, "Failed to create swap chain");

    // Create frame fence (timeline semaphore)
    m_frameFence = m_rhiDevice->CreateFence(0);

    // Create per-frame resources
    m_commandLists.resize(kMaxFramesInFlight);
    m_fenceValues.resize(kMaxFramesInFlight, 0);

    for (uint32 i = 0; i < kMaxFramesInFlight; ++i)
    {
        m_commandLists[i] = m_rhiDevice->CreateCommandList(rhi::RHIQueueType::Graphics);
    }

    uint32 numSwapBuffers = m_swapChain->GetBufferCount();
    m_isFirstFrame.resize(numSwapBuffers, true);

    // Vulkan-specific: create binary semaphores for swapchain acquire/present
    if (m_backend == rhi::RHIBackend::Vulkan)
    {
        m_acquireSemaphores.resize(kMaxFramesInFlight);
        m_presentSemaphores.resize(numSwapBuffers);

        for (uint32 i = 0; i < kMaxFramesInFlight; ++i)
        {
            m_acquireSemaphores[i] = m_rhiDevice->CreateBinarySemaphore();
        }
        for (uint32 i = 0; i < numSwapBuffers; ++i)
        {
            m_presentSemaphores[i] = m_rhiDevice->CreateBinarySemaphore();
        }
    }

    WEST_LOG_INFO(LogCategory::RHI, "Frame-in-Flight initialized (N={}).", kMaxFramesInFlight);

    // Phase 3: Create bindless textured quad resources
    InitializeTexturedQuad();
    RunCommandRecordingBenchmark();
}

void Win32Application::Run()
{
    WEST_LOG_INFO(LogCategory::Core, "Entering main loop...");

    while (m_isRunning)
    {
        m_timer.Tick();
        m_window->PollEvents();

        if (m_window->ShouldClose())
        {
            m_isRunning = false;
            break;
        }

        RenderFrame();

        if (m_maxFrameCount > 0 && m_frameCount >= m_maxFrameCount)
        {
            WEST_LOG_INFO(LogCategory::Core, "Frame limit reached ({} frames).", m_frameCount);
            m_isRunning = false;
        }

        WEST_FRAME_MARK;
    }

    WEST_LOG_INFO(LogCategory::Core, "Main loop exited.");
}

// ── Textured Quad Setup (Phase 3) ─────────────────────────────────────────

void Win32Application::InitializeTexturedQuad()
{
    WEST_PROFILE_FUNCTION();

    struct Vertex
    {
        float position[3];
        float uv[2];
    };

    static constexpr Vertex vertices[] = {
        {{-0.65f,  0.65f, 0.0f}, {0.0f, 0.0f}},
        {{ 0.65f,  0.65f, 0.0f}, {1.0f, 0.0f}},
        {{ 0.65f, -0.65f, 0.0f}, {1.0f, 1.0f}},
        {{-0.65f, -0.65f, 0.0f}, {0.0f, 1.0f}},
    };

    static constexpr uint32 indices[] = {0, 1, 2, 0, 2, 3};

    const uint64_t vbSize = sizeof(vertices);
    const uint64_t ibSize = sizeof(indices);
    const uint32_t vertexStride = sizeof(Vertex);

    rhi::RHIBufferDesc vbStagingDesc{};
    vbStagingDesc.sizeBytes = vbSize;
    vbStagingDesc.structureByteStride = vertexStride;
    vbStagingDesc.usage = rhi::RHIBufferUsage::CopySource;
    vbStagingDesc.memoryType = rhi::RHIMemoryType::Upload;
    vbStagingDesc.debugName = "QuadVB_Staging";

    auto vbStaging = m_rhiDevice->CreateBuffer(vbStagingDesc);
    WEST_CHECK(vbStaging != nullptr, "Failed to create vertex staging buffer");

    void* mapped = vbStaging->Map();
    WEST_CHECK(mapped != nullptr, "Failed to map vertex staging buffer");
    std::memcpy(mapped, vertices, vbSize);
    vbStaging->Unmap();

    rhi::RHIBufferDesc ibStagingDesc{};
    ibStagingDesc.sizeBytes = ibSize;
    ibStagingDesc.structureByteStride = sizeof(uint32);
    ibStagingDesc.usage = rhi::RHIBufferUsage::CopySource;
    ibStagingDesc.memoryType = rhi::RHIMemoryType::Upload;
    ibStagingDesc.debugName = "QuadIB_Staging";

    auto ibStaging = m_rhiDevice->CreateBuffer(ibStagingDesc);
    WEST_CHECK(ibStaging != nullptr, "Failed to create index staging buffer");

    mapped = ibStaging->Map();
    WEST_CHECK(mapped != nullptr, "Failed to map index staging buffer");
    std::memcpy(mapped, indices, ibSize);
    ibStaging->Unmap();

    rhi::RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = vbSize;
    vbDesc.structureByteStride = vertexStride;
    vbDesc.usage = rhi::RHIBufferUsage::VertexBuffer | rhi::RHIBufferUsage::CopyDest;
    vbDesc.memoryType = rhi::RHIMemoryType::GPULocal;
    vbDesc.debugName = "QuadVB";

    m_quadVB = m_rhiDevice->CreateBuffer(vbDesc);
    WEST_CHECK(m_quadVB != nullptr, "Failed to create vertex buffer");

    rhi::RHIBufferDesc ibDesc{};
    ibDesc.sizeBytes = ibSize;
    ibDesc.structureByteStride = sizeof(uint32);
    ibDesc.usage = rhi::RHIBufferUsage::IndexBuffer | rhi::RHIBufferUsage::CopyDest;
    ibDesc.memoryType = rhi::RHIMemoryType::GPULocal;
    ibDesc.debugName = "QuadIB";

    m_quadIB = m_rhiDevice->CreateBuffer(ibDesc);
    WEST_CHECK(m_quadIB != nullptr, "Failed to create index buffer");

    static constexpr uint32 kTextureWidth = 64;
    static constexpr uint32 kTextureHeight = 64;
    static constexpr uint32 kBytesPerPixel = 4;
    std::array<uint32, kTextureWidth * kTextureHeight> checkerPixels{};

    for (uint32 y = 0; y < kTextureHeight; ++y)
    {
        for (uint32 x = 0; x < kTextureWidth; ++x)
        {
            const bool bright = ((x / 8) + (y / 8)) % 2 == 0;
            checkerPixels[y * kTextureWidth + x] = bright ? 0xFFFFF2D0u : 0xFF1A1D2Bu;
        }
    }

    rhi::RHIBufferDesc textureStagingDesc{};
    textureStagingDesc.sizeBytes = checkerPixels.size() * sizeof(uint32);
    textureStagingDesc.usage = rhi::RHIBufferUsage::CopySource;
    textureStagingDesc.memoryType = rhi::RHIMemoryType::Upload;
    textureStagingDesc.debugName = "CheckerTexture_Staging";

    auto textureStaging = m_rhiDevice->CreateBuffer(textureStagingDesc);
    WEST_CHECK(textureStaging != nullptr, "Failed to create texture staging buffer");

    mapped = textureStaging->Map();
    WEST_CHECK(mapped != nullptr, "Failed to map texture staging buffer");
    std::memcpy(mapped, checkerPixels.data(), textureStagingDesc.sizeBytes);
    textureStaging->Unmap();

    rhi::RHITextureDesc textureDesc{};
    textureDesc.width = kTextureWidth;
    textureDesc.height = kTextureHeight;
    textureDesc.format = rhi::RHIFormat::RGBA8_UNORM;
    textureDesc.usage = rhi::RHITextureUsage::ShaderResource | rhi::RHITextureUsage::CopyDest;
    textureDesc.debugName = "CheckerTexture";

    m_checkerTexture = m_rhiDevice->CreateTexture(textureDesc);
    WEST_CHECK(m_checkerTexture != nullptr, "Failed to create checker texture");

    m_checkerSampler = m_rhiDevice->CreateSampler({});
    WEST_CHECK(m_checkerSampler != nullptr, "Failed to create checker sampler");

    const rhi::BindlessIndex textureIndex = m_rhiDevice->RegisterBindlessResource(m_checkerTexture.get());
    const rhi::BindlessIndex samplerIndex = m_rhiDevice->RegisterBindlessResource(m_checkerSampler.get());
    WEST_CHECK(textureIndex != rhi::kInvalidBindlessIndex, "Failed to register checker texture");
    WEST_CHECK(samplerIndex != rhi::kInvalidBindlessIndex, "Failed to register checker sampler");

    auto copyCmdList = m_rhiDevice->CreateCommandList(rhi::RHIQueueType::Graphics);
    copyCmdList->Begin();
    copyCmdList->CopyBuffer(vbStaging.get(), 0, m_quadVB.get(), 0, vbSize);
    copyCmdList->CopyBuffer(ibStaging.get(), 0, m_quadIB.get(), 0, ibSize);

    rhi::RHIBarrierDesc textureToCopy{};
    textureToCopy.type = rhi::RHIBarrierDesc::Type::Transition;
    textureToCopy.texture = m_checkerTexture.get();
    textureToCopy.stateBefore = rhi::RHIResourceState::Undefined;
    textureToCopy.stateAfter = rhi::RHIResourceState::CopyDest;
    copyCmdList->ResourceBarrier(textureToCopy);

    rhi::RHICopyRegion copyRegion{};
    copyRegion.bufferOffset = 0;
    copyRegion.bufferRowLength = kTextureWidth;
    copyRegion.bufferImageHeight = kTextureHeight;
    copyRegion.texWidth = kTextureWidth;
    copyRegion.texHeight = kTextureHeight;
    copyRegion.texDepth = 1;
    copyCmdList->CopyBufferToTexture(textureStaging.get(), m_checkerTexture.get(), copyRegion);

    rhi::RHIBarrierDesc vbBarrier{};
    vbBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
    vbBarrier.buffer = m_quadVB.get();
    vbBarrier.stateBefore = rhi::RHIResourceState::CopyDest;
    vbBarrier.stateAfter = rhi::RHIResourceState::VertexBuffer;
    copyCmdList->ResourceBarrier(vbBarrier);

    rhi::RHIBarrierDesc ibBarrier{};
    ibBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
    ibBarrier.buffer = m_quadIB.get();
    ibBarrier.stateBefore = rhi::RHIResourceState::CopyDest;
    ibBarrier.stateAfter = rhi::RHIResourceState::IndexBuffer;
    copyCmdList->ResourceBarrier(ibBarrier);

    rhi::RHIBarrierDesc textureToShader{};
    textureToShader.type = rhi::RHIBarrierDesc::Type::Transition;
    textureToShader.texture = m_checkerTexture.get();
    textureToShader.stateBefore = rhi::RHIResourceState::CopyDest;
    textureToShader.stateAfter = rhi::RHIResourceState::ShaderResource;
    copyCmdList->ResourceBarrier(textureToShader);

    copyCmdList->End();

    // Submit and wait
    auto copyFence = m_rhiDevice->CreateFence(0);
    rhi::RHISubmitInfo copySubmit{};
    copySubmit.commandList = copyCmdList.get();
    copySubmit.signalFence = copyFence.get();
    copySubmit.signalValue = 1;

    auto* queue = m_rhiDevice->GetQueue(rhi::RHIQueueType::Graphics);
    queue->Submit(copySubmit);
    copyFence->Wait(1);

    WEST_LOG_INFO(LogCategory::RHI, "Textured quad resources uploaded (VB={} bytes, IB={} bytes, texture={}x{}).",
                  vbSize, ibSize, kTextureWidth, kTextureHeight);

    std::span<const uint8_t> vsData;
    std::span<const uint8_t> psData;

    if (m_backend == rhi::RHIBackend::DX12)
    {
        vsData = std::span<const uint8_t>(rhi::kTexturedQuadVS_DXIL, sizeof(rhi::kTexturedQuadVS_DXIL));
        psData = std::span<const uint8_t>(rhi::kTexturedQuadPS_DXIL, sizeof(rhi::kTexturedQuadPS_DXIL));
    }
    else
    {
        vsData = std::span<const uint8_t>(rhi::kTexturedQuadVS_SPIRV, sizeof(rhi::kTexturedQuadVS_SPIRV));
        psData = std::span<const uint8_t>(rhi::kTexturedQuadPS_SPIRV, sizeof(rhi::kTexturedQuadPS_SPIRV));
    }

    rhi::RHIVertexAttribute vertexAttribs[] = {
        {"POSITION", rhi::RHIFormat::RGB32_FLOAT, 0},
        {"TEXCOORD", rhi::RHIFormat::RG32_FLOAT, 12},
    };

    rhi::RHIFormat colorFormat = rhi::RHIFormat::BGRA8_UNORM;

    rhi::RHIGraphicsPipelineDesc pipelineDesc{};
    pipelineDesc.vertexShader = vsData;
    pipelineDesc.fragmentShader = psData;
    pipelineDesc.vertexAttributes = vertexAttribs;
    pipelineDesc.vertexStride = vertexStride;
    pipelineDesc.topology = rhi::RHIPrimitiveTopology::TriangleList;
    pipelineDesc.cullMode = rhi::RHICullMode::None; // See both sides
    pipelineDesc.depthTest = false;
    pipelineDesc.depthWrite = false;
    pipelineDesc.colorFormats = {&colorFormat, 1};
    pipelineDesc.depthFormat = rhi::RHIFormat::Unknown;
    pipelineDesc.debugName = "TexturedQuadPipeline";

    m_texturedQuadPipeline = m_rhiDevice->CreateGraphicsPipeline(pipelineDesc);
    WEST_CHECK(m_texturedQuadPipeline != nullptr, "Failed to create textured quad pipeline");

    WEST_LOG_INFO(LogCategory::RHI, "Bindless textured quad pipeline created (texture={}, sampler={}).",
                  textureIndex, samplerIndex);
}

void Win32Application::RunCommandRecordingBenchmark()
{
    WEST_PROFILE_FUNCTION();

    const uint32 hardwareThreads = std::max<uint32>(1, std::thread::hardware_concurrency());
    const uint32 workerCount = std::clamp<uint32>(hardwareThreads, 2, 4);
    static constexpr uint32 kStateCommandsPerList = 2000;

    std::vector<std::unique_ptr<rhi::IRHICommandList>> benchmarkLists(workerCount);
    for (uint32 i = 0; i < workerCount; ++i)
    {
        benchmarkLists[i] = m_rhiDevice->CreateCommandList(rhi::RHIQueueType::Graphics);
    }

    auto recordList = [](rhi::IRHICommandList* commandList, uint32 seed) {
        commandList->Reset();
        commandList->Begin();
        for (uint32 i = 0; i < kStateCommandsPerList; ++i)
        {
            const float width = 320.0f + static_cast<float>((i + seed) % 64);
            const float height = 180.0f + static_cast<float>((i + seed) % 64);
            commandList->SetViewport(0.0f, 0.0f, width, height);
            commandList->SetScissor(0, 0, static_cast<uint32>(width), static_cast<uint32>(height));
        }
        commandList->End();
    };

    auto now = []() {
        return std::chrono::high_resolution_clock::now();
    };

    const auto singleStart = now();
    for (uint32 i = 0; i < workerCount; ++i)
    {
        recordList(benchmarkLists[i].get(), i);
    }
    const auto singleEnd = now();

    TaskSystem taskSystem;
    taskSystem.Initialize(workerCount);

    const auto multiStart = now();
    taskSystem.Dispatch(workerCount, [&](uint32 taskIndex) {
        recordList(benchmarkLists[taskIndex].get(), taskIndex);
    });
    taskSystem.Wait();
    const auto multiEnd = now();

    taskSystem.Shutdown();

    const double singleMs = std::chrono::duration<double, std::milli>(singleEnd - singleStart).count();
    const double multiMs = std::chrono::duration<double, std::milli>(multiEnd - multiStart).count();

    if (m_backend == rhi::RHIBackend::DX12)
    {
        Logger::Log(LogLevel::Info, LogCategory::RHI,
                    std::format("Command recording benchmark: backend={}, validation={}, dx12GBV={}, "
                                "single-thread {:.3f} ms, multi-thread {:.3f} ms ({} lists).",
                                BackendName(m_backend), OnOff(m_enableValidation),
                                OnOff(m_enableDX12GPUBasedValidation), singleMs, multiMs, workerCount));
    }
    else
    {
        Logger::Log(LogLevel::Info, LogCategory::RHI,
                    std::format("Command recording benchmark: backend={}, validation={}, single-thread {:.3f} ms, "
                                "multi-thread {:.3f} ms ({} lists).",
                                BackendName(m_backend), OnOff(m_enableValidation), singleMs, multiMs, workerCount));
    }
}

void Win32Application::RenderFrame()
{
    WEST_PROFILE_FUNCTION();

    const uint32 windowWidth = m_window->GetWidth();
    const uint32 windowHeight = m_window->GetHeight();
    if (windowWidth == 0 || windowHeight == 0)
    {
        return;
    }

    if (m_swapChain)
    {
        auto* currentBackBuffer = m_swapChain->GetCurrentBackBuffer();
        const auto& currentDesc = currentBackBuffer->GetDesc();
        if (currentDesc.width != windowWidth || currentDesc.height != windowHeight)
        {
            ResizeSwapChain(windowWidth, windowHeight);
            return;
        }
    }

    uint32 frameIndex = static_cast<uint32>(m_frameCount % kMaxFramesInFlight);

    // 1. Wait for the GPU to finish using this frame's resources
    //    (N frames ago if we're ahead)
    m_frameFence->Wait(m_fenceValues[frameIndex]);

    // Now that the fence is reached, we can safely destroy old resources used in that frame
    m_rhiDevice->FlushDeferredDeletions(m_fenceValues[frameIndex]);

    // Set the target fence value for any new resources deleted during this frame
    m_rhiDevice->SetCurrentFrameFenceValue(m_fenceValues[frameIndex] + kMaxFramesInFlight); // roughly target next usage

    // 2. Acquire the next swapchain image
    rhi::IRHISemaphore* acquireSem = nullptr;
    if (m_backend == rhi::RHIBackend::Vulkan)
    {
        acquireSem = m_acquireSemaphores[frameIndex].get();
    }
    uint32 imageIndex = m_swapChain->AcquireNextImage(acquireSem);
    if (imageIndex == UINT32_MAX)
    {
        ResizeSwapChain(windowWidth, windowHeight);
        return;
    }

    // 3. Record commands
    auto* cmdList = m_commandLists[frameIndex].get();
    auto* backBuffer = m_swapChain->GetCurrentBackBuffer();

    cmdList->Reset();
    cmdList->Begin();

    // Transition: Undefined/Present → RenderTarget
    rhi::RHIBarrierDesc barrierToRT{};
    barrierToRT.type = rhi::RHIBarrierDesc::Type::Transition;
    barrierToRT.texture = backBuffer;
    barrierToRT.stateBefore = m_isFirstFrame[imageIndex] ? rhi::RHIResourceState::Undefined : rhi::RHIResourceState::Present;
    barrierToRT.stateAfter = rhi::RHIResourceState::RenderTarget;
    cmdList->ResourceBarrier(barrierToRT);

    m_isFirstFrame[imageIndex] = false;

    // ClearColor — animated cornflower blue
    float time = static_cast<float>(m_frameCount) * 0.01f;
    float r = 0.392f + 0.1f * std::sin(time);
    float g = 0.584f + 0.1f * std::sin(time * 0.7f);
    float b = 0.929f + 0.05f * std::sin(time * 1.3f);

    rhi::RHIColorAttachment colorAttach{};
    colorAttach.texture = backBuffer;
    colorAttach.loadOp = rhi::RHILoadOp::Clear;
    colorAttach.storeOp = rhi::RHIStoreOp::Store;
    colorAttach.clearColor[0] = r;
    colorAttach.clearColor[1] = g;
    colorAttach.clearColor[2] = b;
    colorAttach.clearColor[3] = 1.0f;

    rhi::RHIRenderPassDesc passDesc{};
    passDesc.colorAttachments = {&colorAttach, 1};
    passDesc.debugName = "ClearColor Pass";

    cmdList->BeginRenderPass(passDesc);

    if (m_texturedQuadPipeline && m_quadVB && m_quadIB && m_checkerTexture && m_checkerSampler)
    {
        auto& texDesc = backBuffer->GetDesc();
        cmdList->SetViewport(0.0f, 0.0f, static_cast<float>(texDesc.width),
                             static_cast<float>(texDesc.height));
        cmdList->SetScissor(0, 0, texDesc.width, texDesc.height);

        struct PushConstants
        {
            rhi::BindlessIndex textureIndex;
            rhi::BindlessIndex samplerIndex;
        };

        PushConstants pushConstants{};
        pushConstants.textureIndex = m_checkerTexture->GetBindlessIndex();
        pushConstants.samplerIndex = m_checkerSampler->GetBindlessIndex();

        cmdList->SetPipeline(m_texturedQuadPipeline.get());
        cmdList->SetPushConstants(&pushConstants, sizeof(pushConstants));
        cmdList->SetVertexBuffer(0, m_quadVB.get());
        cmdList->SetIndexBuffer(m_quadIB.get(), rhi::RHIFormat::R32_UINT);
        cmdList->DrawIndexed(6);
    }

    cmdList->EndRenderPass();

    // Transition: RenderTarget → Present
    rhi::RHIBarrierDesc barrierToPresent{};
    barrierToPresent.type = rhi::RHIBarrierDesc::Type::Transition;
    barrierToPresent.texture = backBuffer;
    barrierToPresent.stateBefore = rhi::RHIResourceState::RenderTarget;
    barrierToPresent.stateAfter = rhi::RHIResourceState::Present;
    cmdList->ResourceBarrier(barrierToPresent);

    cmdList->End();

    // 4. Submit + signal fence
    uint64 signalValue = m_frameFence->AdvanceValue();
    m_fenceValues[frameIndex] = signalValue;

    rhi::RHISubmitInfo submitInfo{};
    submitInfo.commandList = cmdList;
    submitInfo.signalFence = m_frameFence.get();
    submitInfo.signalValue = signalValue;

    if (m_backend == rhi::RHIBackend::Vulkan)
    {
        submitInfo.waitSemaphore = m_acquireSemaphores[frameIndex].get();
        submitInfo.signalSemaphore = m_presentSemaphores[imageIndex].get();
    }

    auto* queue = m_rhiDevice->GetQueue(rhi::RHIQueueType::Graphics);
    queue->Submit(submitInfo);

    // 5. Present
    rhi::IRHISemaphore* presentSem = nullptr;
    if (m_backend == rhi::RHIBackend::Vulkan)
    {
        presentSem = m_presentSemaphores[imageIndex].get();
    }
    if (!m_swapChain->Present(presentSem))
    {
        ResizeSwapChain(windowWidth, windowHeight);
    }

    m_frameCount++;
}

void Win32Application::ResizeSwapChain(uint32 width, uint32 height)
{
    if (!m_rhiDevice || !m_swapChain || width == 0 || height == 0)
    {
        return;
    }

    m_rhiDevice->WaitIdle();
    m_swapChain->Resize(width, height);

    const uint32 numSwapBuffers = m_swapChain->GetBufferCount();
    m_isFirstFrame.assign(numSwapBuffers, true);

    if (m_backend == rhi::RHIBackend::Vulkan)
    {
        m_presentSemaphores.clear();
        m_presentSemaphores.resize(numSwapBuffers);
        for (uint32 i = 0; i < numSwapBuffers; ++i)
        {
            m_presentSemaphores[i] = m_rhiDevice->CreateBinarySemaphore();
        }
    }

    Logger::Log(LogLevel::Info, LogCategory::RHI, std::format("SwapChain resize handled: {}x{}", width, height));
}

void Win32Application::Shutdown()
{
    WEST_PROFILE_FUNCTION();
    WEST_LOG_INFO(LogCategory::Core, "Shutting down...");

    ShutdownRHI();

    m_window.reset();

    Logger::Shutdown();
}

void Win32Application::ShutdownRHI()
{
    if (m_rhiDevice)
    {
        m_rhiDevice->WaitIdle();
    }

    // Release in reverse creation order
    if (m_checkerSampler && m_checkerSampler->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
    {
        m_rhiDevice->UnregisterBindlessResource(m_checkerSampler->GetBindlessIndex());
    }
    if (m_checkerTexture && m_checkerTexture->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
    {
        m_rhiDevice->UnregisterBindlessResource(m_checkerTexture->GetBindlessIndex());
    }

    m_texturedQuadPipeline.reset();
    m_checkerSampler.reset();
    m_checkerTexture.reset();
    m_quadIB.reset();
    m_quadVB.reset();
    m_presentSemaphores.clear();
    m_acquireSemaphores.clear();
    m_commandLists.clear();
    m_frameFence.reset();
    m_swapChain.reset();
    m_rhiDevice.reset();

    WEST_LOG_INFO(LogCategory::RHI, "RHI shutdown complete.");
}

// ── Application Factory ──────────────────────────────────────────────────
std::unique_ptr<IApplication> CreateApplication()
{
    return std::make_unique<Win32Application>();
}

} // namespace west
