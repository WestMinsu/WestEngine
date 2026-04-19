// =============================================================================
// Platform (Win32)
// Win32 application lifecycle with RHI ClearColor rendering loop
// =============================================================================
#include "platform/win32/Win32Application.h"

#include "core/Assert.h"
#include "core/Logger.h"
#include "core/Profiler.h"
#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHIFence.h"
#include "rhi/interface/IRHIPipeline.h"
#include "rhi/interface/IRHIQueue.h"
#include "rhi/interface/IRHISemaphore.h"
#include "rhi/interface/IRHISwapChain.h"
#include "rhi/interface/IRHITexture.h"
#include "rhi/interface/RHIDescriptors.h"
#include "rhi/interface/RHIFactory.h"
#include "rhi/common/TriangleShaderData.h"

#include <cmath>
#include <cstring>

namespace west
{

bool Win32Application::Initialize()
{
    WEST_PROFILE_FUNCTION();
    Logger::Initialize();

    WEST_LOG_INFO(LogCategory::Core, "WestEngine v0.1.0 initializing...");

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
        if (std::string_view(argv[i]).find("vulkan") != std::string_view::npos)
        {
            m_backend = rhi::RHIBackend::Vulkan;
            WEST_LOG_INFO(LogCategory::Core, "Vulkan backend selected via command line.");
            break;
        }
    }

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
    config.enableValidation = true;
    config.enableGPUCrashDiag = true;
    config.preferredGPUIndex = UINT32_MAX;
    config.windowHandle = m_window->GetNativeHandle();
    config.windowWidth = m_window->GetWidth();
    config.windowHeight = m_window->GetHeight();

    // Create RHI device
    m_rhiDevice = rhi::RHIFactory::CreateDevice(m_backend, config);
    WEST_CHECK(m_rhiDevice != nullptr, "Failed to create RHI device");

    WEST_LOG_INFO(LogCategory::RHI, "RHI Backend: {}", (m_backend == rhi::RHIBackend::DX12) ? "DX12" : "Vulkan");
    WEST_LOG_INFO(LogCategory::RHI, "GPU: {}", m_rhiDevice->GetDeviceName());

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

    // Phase 2: Create triangle resources
    InitializeTriangle();
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

        WEST_FRAME_MARK;
    }

    WEST_LOG_INFO(LogCategory::Core, "Main loop exited.");
}

// ── Triangle Setup (Phase 2) ──────────────────────────────────────────────

void Win32Application::InitializeTriangle()
{
    WEST_PROFILE_FUNCTION();

    // Vertex data: position (float3) + color (float4)
    struct Vertex
    {
        float position[3];
        float color[4];
    };

    static constexpr Vertex vertices[] = {
        {{  0.0f,  0.5f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }}, // Top    — Red
        {{  0.5f, -0.5f, 0.0f }, { 0.0f, 1.0f, 0.0f, 1.0f }}, // Right  — Green
        {{ -0.5f, -0.5f, 0.0f }, { 0.0f, 0.0f, 1.0f, 1.0f }}, // Left   — Blue
    };

    const uint64_t vbSize = sizeof(vertices);
    const uint32_t vertexStride = sizeof(Vertex);

    // 1. Create staging buffer (Upload heap)
    rhi::RHIBufferDesc stagingDesc{};
    stagingDesc.sizeBytes = vbSize;
    stagingDesc.structureByteStride = vertexStride;
    stagingDesc.usage = rhi::RHIBufferUsage::CopySource;
    stagingDesc.memoryType = rhi::RHIMemoryType::Upload;
    stagingDesc.debugName = "TriangleVB_Staging";

    auto staging = m_rhiDevice->CreateBuffer(stagingDesc);
    WEST_CHECK(staging != nullptr, "Failed to create staging buffer");

    // 2. Copy vertex data to staging buffer
    void* mapped = staging->Map();
    WEST_CHECK(mapped != nullptr, "Failed to map staging buffer");
    std::memcpy(mapped, vertices, vbSize);
    staging->Unmap();

    // 3. Create GPU-local vertex buffer
    rhi::RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = vbSize;
    vbDesc.structureByteStride = vertexStride;
    vbDesc.usage = rhi::RHIBufferUsage::VertexBuffer | rhi::RHIBufferUsage::CopyDest;
    vbDesc.memoryType = rhi::RHIMemoryType::GPULocal;
    vbDesc.debugName = "TriangleVB";

    m_triangleVB = m_rhiDevice->CreateBuffer(vbDesc);
    WEST_CHECK(m_triangleVB != nullptr, "Failed to create vertex buffer");

    // 4. Copy staging → GPU-local using a one-shot command list
    auto copyCmdList = m_rhiDevice->CreateCommandList(rhi::RHIQueueType::Graphics);
    copyCmdList->Begin();
    copyCmdList->CopyBuffer(staging.get(), 0, m_triangleVB.get(), 0, vbSize);

    // Barrier: CopyDest → VertexBuffer
    rhi::RHIBarrierDesc barrier{};
    barrier.type = rhi::RHIBarrierDesc::Type::Transition;
    barrier.buffer = m_triangleVB.get();
    barrier.stateBefore = rhi::RHIResourceState::CopyDest;
    barrier.stateAfter = rhi::RHIResourceState::VertexBuffer;
    copyCmdList->ResourceBarrier(barrier);

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

    WEST_LOG_INFO(LogCategory::RHI, "Triangle vertex buffer uploaded ({} bytes).", vbSize);

    // 5. Create pipeline
    // Select shader bytecodes based on backend
    std::span<const uint8_t> vsData;
    std::span<const uint8_t> psData;

    if (m_backend == rhi::RHIBackend::DX12)
    {
        vsData = std::span<const uint8_t>(rhi::kTriangleVS_DXIL, sizeof(rhi::kTriangleVS_DXIL));
        psData = std::span<const uint8_t>(rhi::kTrianglePS_DXIL, sizeof(rhi::kTrianglePS_DXIL));
    }
    else
    {
        vsData = std::span<const uint8_t>(rhi::kTriangleVS_SPIRV, sizeof(rhi::kTriangleVS_SPIRV));
        psData = std::span<const uint8_t>(rhi::kTrianglePS_SPIRV, sizeof(rhi::kTrianglePS_SPIRV));
    }

    rhi::RHIVertexAttribute vertexAttribs[] = {
        {"POSITION", rhi::RHIFormat::RGB32_FLOAT, 0},
        {"COLOR",    rhi::RHIFormat::RGBA32_FLOAT, 12},
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
    pipelineDesc.debugName = "TrianglePipeline";

    m_trianglePipeline = m_rhiDevice->CreateGraphicsPipeline(pipelineDesc);
    WEST_CHECK(m_trianglePipeline != nullptr, "Failed to create triangle pipeline");

    WEST_LOG_INFO(LogCategory::RHI, "Triangle pipeline created.");
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

    // ── Phase 2: Draw Triangle ────────────────────────────────────────
    if (m_trianglePipeline && m_triangleVB)
    {
        auto& texDesc = backBuffer->GetDesc();
        cmdList->SetViewport(0.0f, 0.0f, static_cast<float>(texDesc.width),
                             static_cast<float>(texDesc.height));
        cmdList->SetScissor(0, 0, texDesc.width, texDesc.height);

        cmdList->SetPipeline(m_trianglePipeline.get());
        cmdList->SetVertexBuffer(0, m_triangleVB.get());
        cmdList->Draw(3); // 3 vertices
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

    WEST_LOG_INFO(LogCategory::RHI, "SwapChain resize handled: {}x{}", width, height);
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
    m_trianglePipeline.reset();
    m_triangleVB.reset();
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
