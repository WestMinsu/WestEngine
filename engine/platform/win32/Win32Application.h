// =============================================================================
// WestEngine - Platform (Win32)
// Win32 application lifecycle with RHI rendering integration
// =============================================================================
#pragma once

#include "core/Timer.h"
#include "platform/IApplication.h"
#include "platform/win32/Win32Window.h"
#include "render/RenderGraph/RenderGraphResource.h"
#include "rhi/interface/RHIEnums.h"

#include <memory>
#include <vector>

namespace west::rhi
{
class IRHIDevice;
class IRHISwapChain;
class IRHIFence;
class IRHISemaphore;
class IRHICommandList;
class IRHIBuffer;
class IRHITexture;
class IRHISampler;
class IRHIPipeline;
} // namespace west::rhi

namespace west::shader
{
class PSOCache;
} // namespace west::shader

namespace west::render
{
class CommandListPool;
class ForwardTexturedQuadPass;
class RenderGraph;
class ToneMappingPass;
class TransientResourcePool;
} // namespace west::render

namespace west
{

class Win32Application final : public IApplication
{
public:
    Win32Application() = default;
    ~Win32Application() override;

    // ── IApplication interface ─────────────────────────────────────────
    [[nodiscard]] bool Initialize() override;
    void Run() override;
    void Shutdown() override;

    /// Access the main window.
    [[nodiscard]] IWindow* GetWindow() const
    {
        return m_window.get();
    }

private:
    void InitializeRHI();
    void InitializeTexturedQuad();
    void RunCommandRecordingBenchmark();
    void ShutdownRHI();
    void RenderFrame();
    void ResizeSwapChain(uint32 width, uint32 height);
    void EnsureFrameGraph(rhi::IRHITexture* backBuffer, rhi::RHIResourceState initialState);

    // ── Platform ──────────────────────────────────────────────────────
    std::unique_ptr<Win32Window> m_window;
    Timer m_timer;
    bool m_isRunning = false;
    uint32 m_maxFrameCount = 0; // 0 = run until the window closes
    bool m_enableValidation = false;
    bool m_enableDX12GPUBasedValidation = false;
    bool m_enableGPUCrashDiag = false;

    // ── RHI ───────────────────────────────────────────────────────────
    rhi::RHIBackend m_backend = rhi::RHIBackend::DX12;

    std::unique_ptr<rhi::IRHIDevice> m_rhiDevice;
    std::unique_ptr<rhi::IRHISwapChain> m_swapChain;
    std::unique_ptr<rhi::IRHIFence> m_frameFence;

    // Per-frame resources (Frame-in-Flight)
    static constexpr uint32 kMaxFramesInFlight = 2;

    std::vector<std::unique_ptr<rhi::IRHISemaphore>> m_acquireSemaphores; // Vulkan only (sized by flight frames)
    std::vector<std::unique_ptr<rhi::IRHISemaphore>> m_presentSemaphores; // Vulkan only (sized by swapchain buffers)
    std::vector<uint64> m_fenceValues;
    std::vector<bool> m_isFirstFrame; // Tracks if a swapchain image is being used for the very first time

    uint64 m_frameCount = 0;

    // ── Phase 3: Bindless Textured Quad Resources ────────────────────
    std::unique_ptr<rhi::IRHIBuffer> m_quadVB;
    std::unique_ptr<rhi::IRHIBuffer> m_quadIB;
    std::unique_ptr<rhi::IRHITexture> m_checkerTexture;
    std::unique_ptr<rhi::IRHISampler> m_checkerSampler;
    std::unique_ptr<shader::PSOCache> m_psoCache;
    std::unique_ptr<render::CommandListPool> m_commandListPool;
    std::unique_ptr<render::RenderGraph> m_frameGraph;
    std::unique_ptr<render::TransientResourcePool> m_transientResourcePool;
    std::unique_ptr<render::ForwardTexturedQuadPass> m_forwardTexturedQuadPass;
    std::unique_ptr<render::ToneMappingPass> m_toneMappingPass;
    render::TextureHandle m_frameBackBufferHandle{};
    render::TextureHandle m_frameSceneColorHandle{};
    uint32 m_frameGraphWidth = 0;
    uint32 m_frameGraphHeight = 0;
    rhi::RHIFormat m_frameGraphBackBufferFormat = rhi::RHIFormat::Unknown;
};

} // namespace west
