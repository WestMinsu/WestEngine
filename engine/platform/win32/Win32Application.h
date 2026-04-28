// =============================================================================
// WestEngine - Platform (Win32)
// Win32 application lifecycle with RHI rendering integration
// =============================================================================
#pragma once

#include "core/Timer.h"
#include "editor/RuntimeRenderSettings.h"
#include "platform/IApplication.h"
#include "platform/win32/Win32Window.h"
#include "render/Passes/BokehDOFPass.h"
#include "render/Passes/ToneMappingPass.h"
#include "render/RenderGraph/RenderGraphResource.h"
#include "rhi/interface/RHIEnums.h"
#include "scene/FreeLookCameraController.h"

#include <filesystem>
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

namespace west::scene
{
class Camera;
class SceneAsset;
} // namespace west::scene

namespace west::render
{
class CommandListPool;
class DeferredLightingPass;
class GBufferPass;
class GPUDrivenCullingPass;
class RenderGraph;
class SSAOPass;
class ShadowMapPass;
class BufferCopyPass;
class TransientResourcePool;
} // namespace west::render

namespace west::editor
{
class FrameTelemetry;
class GPUTimerManager;
class ImGuiPass;
class ImGuiRenderer;
} // namespace west::editor

namespace west
{

class Win32Application final : public IApplication
{
public:
    Win32Application() = default;
    explicit Win32Application(const ApplicationDesc& desc);
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
    struct SceneMeshResource
    {
        std::unique_ptr<rhi::IRHIBuffer> ownedVertexBuffer;
        std::unique_ptr<rhi::IRHIBuffer> ownedIndexBuffer;
        rhi::IRHIBuffer* vertexBuffer = nullptr;
        rhi::IRHIBuffer* indexBuffer = nullptr;
        uint64 vertexOffsetBytes = 0;
        uint64 indexOffsetBytes = 0;
        uint32 indexCount = 0;
        uint32 materialIndex = 0;
    };

    struct SceneTextureResource
    {
        std::filesystem::path sourcePath;
        std::unique_ptr<rhi::IRHITexture> texture;
    };

    void InitializeRHI();
    void InitializeTexturedQuad();
    void InitializeScene();
    void InitializeImGui();
    void InitializeImageBasedLighting(rhi::IRHICommandList& uploadCommandList,
                                      std::vector<std::unique_ptr<rhi::IRHIBuffer>>& stagingBuffers);
    void InitializeFreeLookCamera();
    void RunCommandRecordingBenchmark();
    void ShutdownRHI();
    void RenderFrame();
    void ResizeSwapChain(uint32 width, uint32 height);
    void EnsureFrameGraph(rhi::IRHITexture* backBuffer, rhi::RHIResourceState initialState, uint32 frameIndex);
    void UpdateFreeLookCamera(float deltaSeconds, bool blockMouseLook);
    void UpdateRuntimePostControls(bool blockKeyboardShortcuts);
    void BuildTelemetryOverlay();
    void BuildImGuiControlPanel();
    [[nodiscard]] bool ConsumeKeyPress(int virtualKey);
    void RecordRuntimeBenchmarkFrame(float cpuFrameMs);
    void LogRuntimeBenchmarkResult() const;
    void ApplyPostPreset(uint32 presetIndex, bool logChange);
    void LogRuntimePostControlsHelp() const;
    void LogRuntimePostState(const char* reason) const;
    void UpdateRuntimePostWindowTitle() const;

    // ── Platform ──────────────────────────────────────────────────────
    std::unique_ptr<Win32Window> m_window;
    Timer m_timer;
    bool m_isRunning = false;
    uint32 m_maxFrameCount = 0; // 0 = run until the window closes
    bool m_enableValidation = false;
    bool m_enableDX12GPUBasedValidation = false;
    bool m_enableGPUCrashDiag = false;
    bool m_enableSceneCache = true;
    bool m_enableSceneMerge = true;
    bool m_enableSceneBatchUpload = true;
    bool m_enableTextureCache = true;
    bool m_enableTextureBatchUpload = true;
    uint32 m_sceneTextureMaxDimension = 1024;
    bool m_enableGPUDrivenScene = true;
    std::string m_baseWindowTitle = "WestEngine";
    ApplicationSceneDesc m_sceneDesc;

    struct RuntimeBenchmarkState
    {
        bool enabled = false;
        bool logged = false;
        uint32 warmupFrames = 120;
        uint32 sampleFrames = 600;
        std::vector<float> cpuFrameMs;
        std::vector<float> gpuFrameMs;
    };
    RuntimeBenchmarkState m_runtimeBenchmark;

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
    std::unique_ptr<rhi::IRHISampler> m_materialStableSampler;
    std::unique_ptr<rhi::IRHISampler> m_shadowSampler;
    std::unique_ptr<rhi::IRHISampler> m_iblSampler;
    std::unique_ptr<shader::PSOCache> m_psoCache;
    std::unique_ptr<render::CommandListPool> m_commandListPool;
    std::unique_ptr<render::RenderGraph> m_frameGraph;
    std::unique_ptr<render::TransientResourcePool> m_transientResourcePool;
    std::unique_ptr<render::ShadowMapPass> m_shadowMapPass;
    std::unique_ptr<render::GBufferPass> m_gBufferPass;
    std::unique_ptr<render::GPUDrivenCullingPass> m_gpuDrivenCullingPass;
    std::unique_ptr<render::BufferCopyPass> m_gpuDrivenCountResetPass;
    std::unique_ptr<render::BufferCopyPass> m_gpuDrivenCountReadbackPass;
    std::unique_ptr<render::SSAOPass> m_ssaoPass;
    std::unique_ptr<render::DeferredLightingPass> m_deferredLightingPass;
    std::unique_ptr<render::BokehDOFPass> m_bokehDOFPass;
    std::unique_ptr<render::ToneMappingPass> m_toneMappingPass;
    std::unique_ptr<editor::FrameTelemetry> m_frameTelemetry;
    std::unique_ptr<editor::GPUTimerManager> m_gpuTimerManager;
    std::unique_ptr<editor::ImGuiRenderer> m_imguiRenderer;
    std::unique_ptr<editor::ImGuiPass> m_imguiPass;
    std::unique_ptr<scene::Camera> m_sceneCamera;
    std::unique_ptr<scene::SceneAsset> m_sceneAsset;
    std::unique_ptr<rhi::IRHIBuffer> m_sceneVertexBuffer;
    std::unique_ptr<rhi::IRHIBuffer> m_sceneIndexBuffer;
    std::unique_ptr<rhi::IRHIBuffer> m_sceneDrawBuffer;
    std::unique_ptr<rhi::IRHIBuffer> m_gpuDrivenCountResetBuffer;
    std::vector<std::unique_ptr<rhi::IRHIBuffer>> m_gpuDrivenIndirectArgsBuffers;
    std::vector<std::unique_ptr<rhi::IRHIBuffer>> m_gpuDrivenIndirectCountBuffers;
    std::vector<std::unique_ptr<rhi::IRHIBuffer>> m_gpuDrivenCountReadbackBuffers;
    std::vector<SceneMeshResource> m_sceneMeshResources;
    std::vector<SceneTextureResource> m_sceneTextureResources;
    std::unique_ptr<rhi::IRHITexture> m_iblPrefilteredTexture;
    std::unique_ptr<rhi::IRHITexture> m_iblIrradianceTexture;
    std::unique_ptr<rhi::IRHITexture> m_iblBrdfLutTexture;
    std::vector<std::unique_ptr<rhi::IRHIBuffer>> m_frameConstantsBuffers;
    std::unique_ptr<rhi::IRHIBuffer> m_materialBuffer;
    std::vector<bool> m_gpuDrivenReadbackPending;
    uint32 m_sceneDrawCount = 0;
    uint32 m_lastGPUDrivenVisibleCount = 0;
    uint32 m_lastLoggedVisibleCount = UINT32_MAX;
    bool m_gpuDrivenAvailable = false;
    bool m_gpuDrivenVisibilityLogged = false;
    scene::FreeLookCameraController m_freeLookCamera;
    bool m_hasLastMousePosition = false;
    long m_lastMouseX = 0;
    long m_lastMouseY = 0;
    editor::RuntimeRenderSettings m_runtimeSettings;
    render::TextureHandle m_frameBackBufferHandle{};
    render::TextureHandle m_frameShadowMapHandle{};
    render::TextureHandle m_frameGBufferPositionHandle{};
    render::TextureHandle m_frameGBufferNormalHandle{};
    render::TextureHandle m_frameGBufferAlbedoHandle{};
    render::TextureHandle m_frameAmbientOcclusionHandle{};
    render::TextureHandle m_frameSceneDepthHandle{};
    render::TextureHandle m_frameSceneColorHandle{};
    render::TextureHandle m_frameBokehDOFHandle{};
    render::TextureHandle m_frameIBLPrefilteredHandle{};
    render::TextureHandle m_frameIBLIrradianceHandle{};
    render::TextureHandle m_frameIBLBrdfLutHandle{};
    render::BufferHandle m_frameConstantsBufferHandle{};
    render::BufferHandle m_frameMaterialBufferHandle{};
    render::BufferHandle m_frameSceneDrawBufferHandle{};
    render::BufferHandle m_frameSceneVertexBufferHandle{};
    render::BufferHandle m_frameSceneIndexBufferHandle{};
    render::BufferHandle m_frameGPUDrivenCountResetBufferHandle{};
    render::BufferHandle m_frameGPUDrivenIndirectArgsHandle{};
    render::BufferHandle m_frameGPUDrivenIndirectCountHandle{};
    render::BufferHandle m_frameGPUDrivenReadbackHandle{};
    uint32 m_frameGraphWidth = 0;
    uint32 m_frameGraphHeight = 0;
    rhi::RHIFormat m_frameGraphBackBufferFormat = rhi::RHIFormat::Unknown;
};

} // namespace west
