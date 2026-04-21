// =============================================================================
// Platform (Win32)
// Win32 application lifecycle with RHI ClearColor rendering loop
// =============================================================================
#include "platform/win32/Win32Application.h"

#include "core/Assert.h"
#include "core/Logger.h"
#include "core/Profiler.h"
#include "core/Threading/TaskSystem.h"
#include "generated/ShaderMetadata.h"
#include "render/Passes/ForwardTexturedQuadPass.h"
#include "render/Passes/ToneMappingPass.h"
#include "render/RenderGraph/RenderGraph.h"
#include "render/RenderGraph/TransientResourcePool.h"
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
#include "shader/PSOCache.h"
#include "shader/ShaderCompiler.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
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

Win32Application::~Win32Application() = default;

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
        else if (arg == "--enable-pix")
        {
            m_enablePixCapture = true;
        }

        if (arg == "--smoke-test")
        {
            m_maxFrameCount = 3;
            WEST_LOG_INFO(LogCategory::Core, "Smoke-test mode enabled ({} frames).", m_maxFrameCount);
        }

        static constexpr std::string_view kFramesPrefix = "--frames=";
        static constexpr std::string_view kPixCaptureFramePrefix = "--pix-capture-frame=";
        static constexpr std::string_view kRenderDocCaptureFramePrefix = "--renderdoc-capture-frame=";
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
        else if (arg.starts_with(kPixCaptureFramePrefix))
        {
            const std::string_view frameText = arg.substr(kPixCaptureFramePrefix.size());
            uint32 parsedFrame = 0;
            const char* begin = frameText.data();
            const char* end = begin + frameText.size();
            const auto [ptr, ec] = std::from_chars(begin, end, parsedFrame);
            if (ec == std::errc{} && ptr == end && parsedFrame > 0)
            {
                m_pixCaptureFrame = parsedFrame;
                m_enablePixCapture = true;
                WEST_LOG_INFO(LogCategory::Core, "PIX capture requested for frame {}.", parsedFrame);
            }
            else
            {
                WEST_LOG_WARNING(LogCategory::Core, "Ignoring invalid PIX capture frame argument: {}", arg);
            }
        }
        else if (arg.starts_with(kRenderDocCaptureFramePrefix))
        {
            const std::string_view frameText = arg.substr(kRenderDocCaptureFramePrefix.size());
            uint32 parsedFrame = 0;
            const char* begin = frameText.data();
            const char* end = begin + frameText.size();
            const auto [ptr, ec] = std::from_chars(begin, end, parsedFrame);
            if (ec == std::errc{} && ptr == end && parsedFrame > 0)
            {
                m_renderDocCaptureFrame = parsedFrame;
                WEST_LOG_INFO(LogCategory::Core, "RenderDoc capture requested for frame {}.", parsedFrame);
            }
            else
            {
                WEST_LOG_WARNING(LogCategory::Core, "Ignoring invalid RenderDoc capture frame argument: {}", arg);
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
    if (m_enablePixCapture && m_backend != rhi::RHIBackend::DX12)
    {
        WEST_LOG_WARNING(LogCategory::Core, "Ignoring --enable-pix because the active backend is not DX12.");
        m_enablePixCapture = false;
    }

    Logger::Log(LogLevel::Info, LogCategory::Core,
                std::format("Launch config: build={}, backend={}, validation={}, dx12GBV={}, gpuCrashDiag={}, "
                            "frameLimit={}",
                            BuildConfigName(), BackendName(m_backend), OnOff(m_enableValidation),
                            OnOff(m_enableDX12GPUBasedValidation), OnOff(m_enableGPUCrashDiag), m_maxFrameCount));

    if (m_enablePixCapture)
    {
        m_pixGpuCapturerLoader.Initialize();
        m_pixProgrammaticCapture.Initialize();
    }

    InitializeRHI();
    m_renderDocCapture.Initialize();

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
    m_psoCache = std::make_unique<shader::PSOCache>();

    // Create per-frame resources
    m_fenceValues.resize(kMaxFramesInFlight, 0);

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

    // Create bindless textured quad resources
    InitializeTexturedQuad();
    m_transientResourcePool = std::make_unique<render::TransientResourcePool>();
    m_forwardTexturedQuadPass = std::make_unique<render::ForwardTexturedQuadPass>(
        *m_rhiDevice, *m_psoCache, m_backend, m_quadVB.get(), m_quadIB.get(), m_checkerTexture.get(),
        m_checkerSampler.get());
    m_toneMappingPass = std::make_unique<render::ToneMappingPass>(
        *m_rhiDevice, *m_psoCache, m_backend, m_checkerSampler.get());
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

// ── Textured Quad Setup ─────────────────────────────────────────

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
    std::vector<rhi::IRHICommandList*> copyCommandLists = {copyCmdList.get()};
    std::vector<rhi::RHITimelineSignalDesc> copySignals = {{copyFence.get(), 1}};
    rhi::RHISubmitInfo copySubmit{};
    copySubmit.commandLists =
        std::span<rhi::IRHICommandList* const>(copyCommandLists.data(), copyCommandLists.size());
    copySubmit.timelineSignals =
        std::span<const rhi::RHITimelineSignalDesc>(copySignals.data(), copySignals.size());

    auto* queue = m_rhiDevice->GetQueue(rhi::RHIQueueType::Graphics);
    queue->Submit(copySubmit);
    copyFence->Wait(1);

    WEST_LOG_INFO(LogCategory::RHI, "Textured quad resources uploaded (VB={} bytes, IB={} bytes, texture={}x{}).",
                  vbSize, ibSize, kTextureWidth, kTextureHeight);
    WEST_LOG_INFO(LogCategory::RHI, "Bindless textured quad resources registered (texture={}, sampler={}).",
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

    bool didBeginPixCapture = false;
    const bool shouldCaptureWithPix =
        m_pixCaptureFrame > 0 && (m_frameCount + 1) == m_pixCaptureFrame && m_pixProgrammaticCapture.IsAvailable();
    if (shouldCaptureWithPix)
    {
        std::error_code error;
        const std::filesystem::path captureDirectory = std::filesystem::current_path() / "artifacts" / "pix";
        std::filesystem::create_directories(captureDirectory, error);
        if (error)
        {
            WEST_LOG_WARNING(LogCategory::Core, "Failed to create PIX capture directory '{}': {}",
                             captureDirectory.string(), error.message());
        }
        else
        {
            const std::filesystem::path capturePath = captureDirectory / "phase5-dx12.wpix";
            WEST_LOG_INFO(LogCategory::Core, "Starting PIX capture for frame {}.", m_frameCount + 1);
            didBeginPixCapture = m_pixProgrammaticCapture.BeginGpuCapture(capturePath.wstring());
        }
    }

    const bool shouldCaptureWithRenderDoc =
        m_renderDocCaptureFrame > 0 && (m_frameCount + 1) == m_renderDocCaptureFrame && m_renderDocCapture.IsAvailable();
    if (shouldCaptureWithRenderDoc)
    {
        WEST_LOG_INFO(LogCategory::Core, "Starting RenderDoc capture for frame {}.", m_frameCount + 1);
        m_renderDocCapture.BeginFrameCapture();
    }

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

    // 3. Build, compile, and execute the frame Render Graph
    auto* backBuffer = m_swapChain->GetCurrentBackBuffer();
    float time = static_cast<float>(m_frameCount) * 0.01f;
    float r = 0.392f + 0.1f * std::sin(time);
    float g = 0.584f + 0.1f * std::sin(time * 0.7f);
    float b = 0.929f + 0.05f * std::sin(time * 1.3f);
    WEST_ASSERT(m_forwardTexturedQuadPass != nullptr);
    WEST_ASSERT(m_toneMappingPass != nullptr);
    WEST_ASSERT(m_transientResourcePool != nullptr);

    render::RenderGraph graph;
    const render::TextureHandle backBufferHandle =
        graph.ImportTexture(backBuffer,
                            m_isFirstFrame[imageIndex] ? rhi::RHIResourceState::Undefined
                                                       : rhi::RHIResourceState::Present,
                            rhi::RHIResourceState::Present, "SwapchainBackBuffer");

    rhi::RHITextureDesc sceneColorDesc{};
    sceneColorDesc.width = backBuffer->GetDesc().width;
    sceneColorDesc.height = backBuffer->GetDesc().height;
    sceneColorDesc.format = rhi::RHIFormat::RGBA16_FLOAT;
    sceneColorDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::ShaderResource;
    sceneColorDesc.debugName = "SceneColorHDR";

    const render::TextureHandle sceneColorHandle = graph.CreateTransientTexture(sceneColorDesc);
    m_forwardTexturedQuadPass->Configure(sceneColorHandle, {r, g, b, 1.0f});
    m_toneMappingPass->Configure(sceneColorHandle, backBufferHandle);
    graph.AddPass(*m_forwardTexturedQuadPass);
    graph.AddPass(*m_toneMappingPass);
    graph.Compile();

    render::RenderGraph::ExecuteDesc executeDesc{
        .device = *m_rhiDevice,
        .timelineFence = *m_frameFence,
        .transientResourcePool = *m_transientResourcePool,
        .waitSemaphore = acquireSem,
        .signalSemaphore = m_backend == rhi::RHIBackend::Vulkan ? m_presentSemaphores[imageIndex].get() : nullptr,
    };

    m_fenceValues[frameIndex] = graph.Execute(executeDesc);
    m_isFirstFrame[imageIndex] = false;

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

    if (didBeginPixCapture)
    {
        m_frameFence->Wait(m_fenceValues[frameIndex]);
        const bool captureSaved = m_pixProgrammaticCapture.EndCapture();
        WEST_LOG_INFO(LogCategory::Core, "PIX capture for frame {} {}.", m_frameCount + 1,
                      captureSaved ? "saved" : "ended without a saved capture");
    }

    if (shouldCaptureWithRenderDoc)
    {
        const bool captureSaved = m_renderDocCapture.EndFrameCapture();
        WEST_LOG_INFO(LogCategory::Core, "RenderDoc capture for frame {} {}.", m_frameCount + 1,
                      captureSaved ? "saved" : "ended without a saved capture");
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

    if (m_transientResourcePool)
    {
        m_transientResourcePool->Reset(m_rhiDevice.get());
    }

    m_toneMappingPass.reset();
    m_forwardTexturedQuadPass.reset();
    m_transientResourcePool.reset();
    m_psoCache.reset();
    m_checkerSampler.reset();
    m_checkerTexture.reset();
    m_quadIB.reset();
    m_quadVB.reset();
    m_presentSemaphores.clear();
    m_acquireSemaphores.clear();
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
