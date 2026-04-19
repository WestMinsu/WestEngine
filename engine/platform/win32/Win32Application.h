// =============================================================================
// WestEngine - Platform (Win32)
// Win32 application lifecycle with RHI rendering integration
// =============================================================================
#pragma once

#include "core/Timer.h"
#include "platform/IApplication.h"
#include "platform/win32/Win32Window.h"
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
class IRHIPipeline;
} // namespace west::rhi

namespace west
{

class Win32Application final : public IApplication
{
public:
    Win32Application() = default;
    ~Win32Application() override = default;

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
    void InitializeTriangle();
    void ShutdownRHI();
    void RenderFrame();
    void ResizeSwapChain(uint32 width, uint32 height);

    // ── Platform ──────────────────────────────────────────────────────
    std::unique_ptr<Win32Window> m_window;
    Timer m_timer;
    bool m_isRunning = false;

    // ── RHI ───────────────────────────────────────────────────────────
    rhi::RHIBackend m_backend = rhi::RHIBackend::DX12;

    std::unique_ptr<rhi::IRHIDevice> m_rhiDevice;
    std::unique_ptr<rhi::IRHISwapChain> m_swapChain;
    std::unique_ptr<rhi::IRHIFence> m_frameFence;

    // Per-frame resources (Frame-in-Flight)
    static constexpr uint32 kMaxFramesInFlight = 2;

    std::vector<std::unique_ptr<rhi::IRHICommandList>> m_commandLists;
    std::vector<std::unique_ptr<rhi::IRHISemaphore>> m_acquireSemaphores; // Vulkan only (sized by flight frames)
    std::vector<std::unique_ptr<rhi::IRHISemaphore>> m_presentSemaphores; // Vulkan only (sized by swapchain buffers)
    std::vector<uint64> m_fenceValues;
    std::vector<bool> m_isFirstFrame; // Tracks if a swapchain image is being used for the very first time

    uint64 m_frameCount = 0;

    // ── Phase 2: Triangle Resources ───────────────────────────────────
    std::unique_ptr<rhi::IRHIBuffer> m_triangleVB;
    std::unique_ptr<rhi::IRHIPipeline> m_trianglePipeline;
};

} // namespace west
