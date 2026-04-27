// =============================================================================
// Platform (Win32)
// Win32 application lifecycle with RHI ClearColor rendering loop
// =============================================================================
#include "platform/win32/Win32Application.h"

#include "core/Assert.h"
#include "core/Logger.h"
#include "core/Profiler.h"
#include "core/Threading/TaskSystem.h"
#include "editor/ImGuiPass.h"
#include "editor/ImGuiRenderer.h"
#include "editor/PostProcessingPresets.h"
#include "editor/Profiler/FrameTelemetry.h"
#include "editor/Profiler/GPUTimerManager.h"
#include "editor/RuntimeControlPanel.h"
#include "editor/TelemetryOverlay.h"
#include "platform/win32/Win32Headers.h"
#include "render/Passes/BokehDOFPass.h"
#include "render/Passes/BufferCopyPass.h"
#include "render/Passes/DeferredLightingPass.h"
#include "render/Passes/GBufferPass.h"
#include "render/Passes/GPUDrivenCullingPass.h"
#include "render/Passes/ShadowMapPass.h"
#include "render/Passes/SSAOPass.h"
#include "render/Passes/ToneMappingPass.h"
#include "render/RenderGraph/CommandListPool.h"
#include "render/RenderGraph/RenderGraph.h"
#include "render/RenderGraph/TransientResourcePool.h"
#include "rhi/common/FormatConversion.h"
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
#include "scene/Camera.h"
#include "scene/ImageData.h"
#include "scene/SceneAsset.h"
#include "scene/SceneAssetLoader.h"
#include "scene/SceneMath.h"
#include "scene/TextureAssetData.h"
#include "shader/PSOCache.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cfloat>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <imgui.h>
#include <numbers>
#include <span>
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

[[nodiscard]] bool ContainsCaseInsensitive(std::string_view text, std::string_view needle)
{
    auto toLower = [](char value) { return static_cast<char>(std::tolower(static_cast<unsigned char>(value))); };

    return std::search(text.begin(), text.end(), needle.begin(), needle.end(),
                       [&](char lhs, char rhs) { return toLower(lhs) == toLower(rhs); }) != text.end();
}

[[nodiscard]] bool IsBistroMetalMaterial(std::string_view materialName)
{
    return ContainsCaseInsensitive(materialName, "metal");
}

[[nodiscard]] editor::ImGuiRenderer::InputState BuildImGuiInputState(HWND hwnd, uint32 width, uint32 height,
                                                                     float deltaSeconds)
{
    editor::ImGuiRenderer::InputState input{};
    input.deltaSeconds = deltaSeconds;
    input.displayWidth = static_cast<float>(width);
    input.displayHeight = static_cast<float>(height);
    input.mouseButtons[0] = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    input.mouseButtons[1] = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    input.mouseButtons[2] = (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0;

    if (hwnd == nullptr)
    {
        return input;
    }

    POINT cursorPosition{};
    if (GetCursorPos(&cursorPosition) == 0 || ScreenToClient(hwnd, &cursorPosition) == 0)
    {
        return input;
    }

    input.mouseX = static_cast<float>(cursorPosition.x);
    input.mouseY = static_cast<float>(cursorPosition.y);
    input.mouseInsideWindow = cursorPosition.x >= 0 && cursorPosition.y >= 0 &&
                              cursorPosition.x < static_cast<LONG>(width) &&
                              cursorPosition.y < static_cast<LONG>(height);
    return input;
}

struct GPUFrameConstants
{
    float viewProjection[16] = {};
    float cameraPosition[4] = {};
    float cameraForward[4] = {};
    float cameraRight[4] = {};
    float cameraUp[4] = {};
    float cameraProjectionParams[4] = {};
    float cameraFrustumParams[4] = {};
    float lightDirection[4] = {};
    float lightColor[4] = {};
    float ambientColor[4] = {};
    float skyZenithColor[4] = {};
    float skyHorizonColor[4] = {};
    float groundColor[4] = {};
    float skyParams[4] = {};
    float iblParams[4] = {};
    float lightViewProjection[16] = {};
    float shadowParams[4] = {};
    float renderFeatureFlags[4] = {};
    float materialParams[4] = {};
    float pbrParams[4] = {};
    float ssaoParams[4] = {};
};

struct GPUDescriptorHandle
{
    rhi::BindlessIndex index = rhi::kInvalidBindlessIndex;
    uint32_t unused = 0;
};

struct GPUMaterialData
{
    float baseColor[4] = {};
    GPUDescriptorHandle baseColorTexture;
    GPUDescriptorHandle opacityTexture;
    float roughness = 0.5f;
    float metallic = 0.0f;
    float alphaCutoff = 0.0f;
    uint32_t hasBaseColorTexture = 0;
    uint32_t hasOpacityTexture = 0;
    // Keep stride at 64 bytes so StructuredBuffer<MaterialData> indexing is stable on Vulkan.
    uint32_t padding0 = 0;
    uint32_t padding1 = 0;
    uint32_t padding2 = 0;
};

static_assert(sizeof(GPUFrameConstants) == 432);
static_assert(sizeof(GPUDescriptorHandle) == 8);
static_assert(sizeof(GPUMaterialData) == 64);

[[nodiscard]] float DegreesToRadians(float degrees)
{
    return degrees * (std::numbers::pi_v<float> / 180.0f);
}

[[nodiscard]] float RadiansToDegrees(float radians)
{
    return radians * (180.0f / std::numbers::pi_v<float>);
}

[[nodiscard]] std::array<float, 3> MakeDirectionalLightVector(float azimuthDegrees, float elevationDegrees)
{
    const float azimuthRadians = DegreesToRadians(azimuthDegrees);
    const float elevationRadians = DegreesToRadians(elevationDegrees);
    const float horizontalScale = std::cos(elevationRadians);

    return scene::Normalize3({
        horizontalScale * std::cos(azimuthRadians),
        -std::sin(elevationRadians),
        horizontalScale * std::sin(azimuthRadians),
    });
}

[[nodiscard]] uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
    WEST_ASSERT(alignment > 0);
    return (value + alignment - 1u) & ~(alignment - 1u);
}

[[nodiscard]] uint64_t AlignUp(uint64_t value, uint64_t alignment)
{
    WEST_ASSERT(alignment > 0);
    return (value + alignment - 1ull) & ~(alignment - 1ull);
}

[[nodiscard]] std::array<float, 16> CreateOrthographicOffCenterRH(float left, float right, float bottom, float top,
                                                                  float nearPlane, float farPlane)
{
    const float invWidth = 1.0f / (right - left);
    const float invHeight = 1.0f / (top - bottom);
    const float invDepth = 1.0f / (nearPlane - farPlane);

    return {
        2.0f * invWidth,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        2.0f * invHeight,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        invDepth,
        0.0f,
        (left + right) * -invWidth,
        (top + bottom) * -invHeight,
        nearPlane * invDepth,
        1.0f,
    };
}

struct Bounds3
{
    std::array<float, 3> min = {FLT_MAX, FLT_MAX, FLT_MAX};
    std::array<float, 3> max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
};

[[nodiscard]] Bounds3 ComputeMeshBounds(const scene::MeshData& mesh)
{
    Bounds3 bounds{};
    for (const scene::MeshVertex& vertex : mesh.vertices)
    {
        bounds.min[0] = std::min(bounds.min[0], vertex.position[0]);
        bounds.min[1] = std::min(bounds.min[1], vertex.position[1]);
        bounds.min[2] = std::min(bounds.min[2], vertex.position[2]);
        bounds.max[0] = std::max(bounds.max[0], vertex.position[0]);
        bounds.max[1] = std::max(bounds.max[1], vertex.position[1]);
        bounds.max[2] = std::max(bounds.max[2], vertex.position[2]);
    }

    if (mesh.vertices.empty())
    {
        bounds.min = {0.0f, 0.0f, 0.0f};
        bounds.max = {0.0f, 0.0f, 0.0f};
    }

    return bounds;
}

[[nodiscard]] Bounds3 ComputeWorldBounds(const Bounds3& localBounds, const std::array<float, 16>& modelMatrix)
{
    const std::array<float, 3> localMin = localBounds.min;
    const std::array<float, 3> localMax = localBounds.max;
    const std::array<float, 3> corners[8] = {
        std::array<float, 3>{localMin[0], localMin[1], localMin[2]},
        std::array<float, 3>{localMax[0], localMin[1], localMin[2]},
        std::array<float, 3>{localMin[0], localMax[1], localMin[2]},
        std::array<float, 3>{localMax[0], localMax[1], localMin[2]},
        std::array<float, 3>{localMin[0], localMin[1], localMax[2]},
        std::array<float, 3>{localMax[0], localMin[1], localMax[2]},
        std::array<float, 3>{localMin[0], localMax[1], localMax[2]},
        std::array<float, 3>{localMax[0], localMax[1], localMax[2]},
    };

    Bounds3 worldBounds{};
    for (const std::array<float, 3>& corner : corners)
    {
        const std::array<float, 3> transformed = scene::TransformPointHomogeneous(modelMatrix, corner);
        worldBounds.min[0] = std::min(worldBounds.min[0], transformed[0]);
        worldBounds.min[1] = std::min(worldBounds.min[1], transformed[1]);
        worldBounds.min[2] = std::min(worldBounds.min[2], transformed[2]);
        worldBounds.max[0] = std::max(worldBounds.max[0], transformed[0]);
        worldBounds.max[1] = std::max(worldBounds.max[1], transformed[1]);
        worldBounds.max[2] = std::max(worldBounds.max[2], transformed[2]);
    }

    return worldBounds;
}

[[nodiscard]] std::array<float, 4> ComputeBoundsSphere(const Bounds3& worldBounds)
{
    const std::array<float, 3> center = {
        (worldBounds.min[0] + worldBounds.max[0]) * 0.5f,
        (worldBounds.min[1] + worldBounds.max[1]) * 0.5f,
        (worldBounds.min[2] + worldBounds.max[2]) * 0.5f,
    };
    const std::array<float, 3> extents = {
        (worldBounds.max[0] - worldBounds.min[0]) * 0.5f,
        (worldBounds.max[1] - worldBounds.min[1]) * 0.5f,
        (worldBounds.max[2] - worldBounds.min[2]) * 0.5f,
    };

    return {center[0], center[1], center[2], scene::Length3(extents)};
}

[[nodiscard]] std::filesystem::path FindAmazonLumberyardBistroRoot()
{
    std::error_code errorCode;
    std::filesystem::path probe = std::filesystem::current_path(errorCode);
    if (errorCode)
    {
        return std::filesystem::path("assets/models/AmazonLumberyardBistro");
    }

    for (;;)
    {
        const std::filesystem::path candidate = probe / "assets" / "models" / "AmazonLumberyardBistro";
        if (std::filesystem::exists(candidate / "Exterior" / "exterior.obj"))
        {
            return candidate;
        }

        const std::filesystem::path parent = probe.parent_path();
        if (parent.empty() || parent == probe)
        {
            break;
        }
        probe = parent;
    }

    return std::filesystem::path("assets/models/AmazonLumberyardBistro");
}

[[nodiscard]] std::string SceneDisplayName(const ApplicationSceneDesc& sceneDesc)
{
    if (!sceneDesc.name.empty())
    {
        return sceneDesc.name;
    }

    if (!sceneDesc.path.empty())
    {
        return sceneDesc.path.stem().string();
    }

    return sceneDesc.kind == ApplicationSceneKind::Bistro ? "bistro" : "static-scene";
}

[[nodiscard]] std::filesystem::path FindCanonicalStaticScenePath()
{
    std::error_code errorCode;
    std::filesystem::path probe = std::filesystem::current_path(errorCode);
    if (errorCode)
    {
        return std::filesystem::path("assets/models/CanonicalStaticScene/CanonicalStaticScene.gltf");
    }

    for (;;)
    {
        const std::filesystem::path candidate =
            probe / "assets" / "models" / "CanonicalStaticScene" / "CanonicalStaticScene.gltf";
        if (std::filesystem::exists(candidate))
        {
            return candidate;
        }

        const std::filesystem::path parent = probe.parent_path();
        if (parent.empty() || parent == probe)
        {
            break;
        }
        probe = parent;
    }

    return std::filesystem::path("assets/models/CanonicalStaticScene/CanonicalStaticScene.gltf");
}

[[nodiscard]] std::filesystem::path ResolveScenePath(const std::filesystem::path& scenePath)
{
    if (scenePath.empty() || scenePath.is_absolute())
    {
        return scenePath;
    }

    std::error_code errorCode;
    std::filesystem::path probe = std::filesystem::current_path(errorCode);
    if (errorCode)
    {
        return scenePath;
    }

    for (;;)
    {
        const std::filesystem::path candidate = probe / scenePath;
        if (std::filesystem::exists(candidate))
        {
            return candidate;
        }

        const std::filesystem::path parent = probe.parent_path();
        if (parent.empty() || parent == probe)
        {
            break;
        }
        probe = parent;
    }

    return scenePath;
}

[[nodiscard]] std::filesystem::path FindGoldenGateHillsRoot()
{
    std::error_code errorCode;
    std::filesystem::path probe = std::filesystem::current_path(errorCode);
    if (errorCode)
    {
        return std::filesystem::path("assets/textures/golden_gate_hills_4k");
    }

    for (;;)
    {
        const std::filesystem::path candidate = probe / "assets" / "textures" / "golden_gate_hills_4k";
        if (std::filesystem::exists(candidate / "specularGgx.ktx2") &&
            std::filesystem::exists(candidate / "diffuseLambertian.ktx2") &&
            std::filesystem::exists(candidate / "outputLUT.png"))
        {
            return candidate;
        }

        const std::filesystem::path parent = probe.parent_path();
        if (parent.empty() || parent == probe)
        {
            break;
        }
        probe = parent;
    }

    return std::filesystem::path("assets/textures/golden_gate_hills_4k");
}

struct StagedTextureSubresource
{
    const scene::TextureSubresourceData* source = nullptr;
    uint32_t alignedRowPitchBytes = 0;
    uint64_t stagingOffsetBytes = 0;
    uint64_t stagingBytes = 0;
};

struct UploadedTextureResult
{
    std::unique_ptr<rhi::IRHITexture> texture;
    uint64_t stagingBytes = 0;
};

[[nodiscard]] uint32_t GetTextureUploadRowCount(rhi::RHIFormat format, uint32_t height)
{
    const uint32_t blockHeight = rhi::GetFormatBlockHeight(format);
    return (height + blockHeight - 1u) / blockHeight;
}

[[nodiscard]] uint32_t GetTextureUploadRowLengthTexels(rhi::RHIFormat format, uint32_t rowPitchBytes)
{
    const uint32_t bytesPerBlock = rhi::GetFormatByteSize(format);
    const uint32_t blockWidth = rhi::GetFormatBlockWidth(format);
    WEST_CHECK(bytesPerBlock > 0, "Unsupported texture upload format");
    WEST_CHECK((rowPitchBytes % bytesPerBlock) == 0, "Texture upload row pitch must be block-size aligned");
    return (rowPitchBytes / bytesPerBlock) * blockWidth;
}

[[nodiscard]] UploadedTextureResult UploadTextureAsset(rhi::IRHIDevice& device, rhi::IRHICommandList& uploadCommandList,
                                                       const scene::TextureAssetData& asset,
                                                       std::vector<std::unique_ptr<rhi::IRHIBuffer>>& stagingBuffers)
{
    WEST_CHECK(rhi::GetFormatByteSize(asset.format) > 0, "Unsupported texture upload format");

    std::vector<StagedTextureSubresource> stagedSubresources;
    stagedSubresources.reserve(asset.subresources.size());
    uint64_t totalStagingBytes = 0;

    for (const scene::TextureSubresourceData& subresource : asset.subresources)
    {
        WEST_CHECK(subresource.rowPitchBytes > 0, "Texture subresource row pitch must be non-zero");

        StagedTextureSubresource staged{};
        staged.source = &subresource;
        staged.alignedRowPitchBytes = AlignUp(subresource.rowPitchBytes, 256u);
        staged.stagingOffsetBytes = AlignUp(totalStagingBytes, 512ull);
        staged.stagingBytes = static_cast<uint64_t>(staged.alignedRowPitchBytes) *
                              GetTextureUploadRowCount(asset.format, subresource.height) * subresource.depth;
        totalStagingBytes = staged.stagingOffsetBytes + staged.stagingBytes;
        stagedSubresources.push_back(staged);
    }

    WEST_CHECK(totalStagingBytes > 0, "Texture asset must have uploadable subresources");

    rhi::RHIBufferDesc stagingDesc{};
    stagingDesc.sizeBytes = totalStagingBytes;
    stagingDesc.usage = rhi::RHIBufferUsage::CopySource;
    stagingDesc.memoryType = rhi::RHIMemoryType::Upload;
    stagingDesc.debugName = "EnvironmentTextureStaging";

    auto stagingBuffer = device.CreateBuffer(stagingDesc);
    WEST_CHECK(stagingBuffer != nullptr, "Failed to create environment texture staging buffer");

    auto* mappedTexture = static_cast<uint8_t*>(stagingBuffer->Map());
    WEST_CHECK(mappedTexture != nullptr, "Failed to map environment texture staging buffer");
    std::memset(mappedTexture, 0, static_cast<size_t>(totalStagingBytes));

    for (const StagedTextureSubresource& staged : stagedSubresources)
    {
        WEST_ASSERT(staged.source != nullptr);
        const scene::TextureSubresourceData& source = *staged.source;
        const uint32_t rowCount = GetTextureUploadRowCount(asset.format, source.height);
        for (uint32_t row = 0; row < rowCount; ++row)
        {
            const uint8_t* sourceRow =
                asset.bytes.data() + source.sourceOffsetBytes + (static_cast<size_t>(row) * source.rowPitchBytes);
            uint8_t* destinationRow =
                mappedTexture + staged.stagingOffsetBytes + (static_cast<size_t>(row) * staged.alignedRowPitchBytes);
            std::memcpy(destinationRow, sourceRow, source.rowPitchBytes);
        }
    }

    stagingBuffer->Unmap();

    rhi::RHITextureDesc textureDesc{};
    textureDesc.width = asset.width;
    textureDesc.height = asset.height;
    textureDesc.depth = asset.depth;
    textureDesc.mipLevels = asset.mipLevels;
    textureDesc.arrayLayers = asset.arrayLayers;
    textureDesc.format = asset.format;
    textureDesc.dimension = asset.dimension;
    textureDesc.usage = rhi::RHITextureUsage::ShaderResource | rhi::RHITextureUsage::CopyDest;
    textureDesc.debugName = asset.debugName.c_str();

    auto texture = device.CreateTexture(textureDesc);
    WEST_CHECK(texture != nullptr, "Failed to create environment texture");

    rhi::RHIBarrierDesc toCopyBarrier{};
    toCopyBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
    toCopyBarrier.texture = texture.get();
    toCopyBarrier.stateBefore = rhi::RHIResourceState::Undefined;
    toCopyBarrier.stateAfter = rhi::RHIResourceState::CopyDest;
    uploadCommandList.ResourceBarrier(toCopyBarrier);

    for (const StagedTextureSubresource& staged : stagedSubresources)
    {
        WEST_ASSERT(staged.source != nullptr);
        const scene::TextureSubresourceData& source = *staged.source;

        rhi::RHICopyRegion copyRegion{};
        copyRegion.bufferOffset = staged.stagingOffsetBytes;
        copyRegion.bufferRowLength = GetTextureUploadRowLengthTexels(asset.format, staged.alignedRowPitchBytes);
        copyRegion.bufferImageHeight = source.height;
        copyRegion.texWidth = source.width;
        copyRegion.texHeight = source.height;
        copyRegion.texDepth = source.depth;
        copyRegion.mipLevel = source.mipLevel;
        copyRegion.arrayLayer = source.arrayLayer;
        uploadCommandList.CopyBufferToTexture(stagingBuffer.get(), texture.get(), copyRegion);
    }

    rhi::RHIBarrierDesc toShaderBarrier{};
    toShaderBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
    toShaderBarrier.texture = texture.get();
    toShaderBarrier.stateBefore = rhi::RHIResourceState::CopyDest;
    toShaderBarrier.stateAfter = rhi::RHIResourceState::ShaderResource;
    uploadCommandList.ResourceBarrier(toShaderBarrier);

    WEST_CHECK(device.RegisterBindlessResource(texture.get()) != rhi::kInvalidBindlessIndex,
               "Failed to register environment texture");

    stagingBuffers.push_back(std::move(stagingBuffer));

    UploadedTextureResult result{};
    result.texture = std::move(texture);
    result.stagingBytes = totalStagingBytes;
    return result;
}

} // namespace

Win32Application::Win32Application(const ApplicationDesc& desc) : m_baseWindowTitle(desc.windowTitle), m_sceneDesc(desc.scene)
{
}

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
    windowDesc.title = m_baseWindowTitle;
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
        else if (arg == "--disable-scene-cache")
        {
            m_enableSceneCache = false;
        }
        else if (arg == "--disable-scene-merge")
        {
            m_enableSceneMerge = false;
        }
        else if (arg == "--disable-scene-batch-upload")
        {
            m_enableSceneBatchUpload = false;
        }
        else if (arg == "--disable-texture-cache")
        {
            m_enableTextureCache = false;
        }
        else if (arg == "--disable-texture-batch-upload")
        {
            m_enableTextureBatchUpload = false;
        }
        else if (arg == "--texture-native-resolution")
        {
            m_sceneTextureMaxDimension = 0;
        }
        else if (arg == "--disable-gpu-driven-scene")
        {
            m_enableGPUDrivenScene = false;
        }

        static constexpr std::string_view kScenePrefix = "--scene=";
        if (arg.starts_with(kScenePrefix))
        {
            const std::string_view sceneName = arg.substr(kScenePrefix.size());
            if (sceneName == "bistro")
            {
                m_sceneDesc = ApplicationSceneDesc{};
            }
            else if (sceneName == "canonical" || sceneName == "canonical-gltf")
            {
                m_sceneDesc.kind = ApplicationSceneKind::StaticScene;
                m_sceneDesc.path = FindCanonicalStaticScenePath();
                m_sceneDesc.name = "canonical-gltf";
                m_sceneDesc.uniformScale = 1.0f;
            }
            else
            {
                WEST_LOG_WARNING(LogCategory::Core, "Ignoring unknown scene preset argument: {}", arg);
            }
        }

        static constexpr std::string_view kModelPrefix = "--model=";
        if (arg.starts_with(kModelPrefix))
        {
            const std::string_view modelText = arg.substr(kModelPrefix.size());
            m_sceneDesc.kind = ApplicationSceneKind::StaticScene;
            m_sceneDesc.path = std::filesystem::path(std::string(modelText));
            m_sceneDesc.name = m_sceneDesc.path.stem().string();
            m_sceneDesc.uniformScale = 1.0f;
            WEST_LOG_INFO(LogCategory::Core, "Static scene model set to {}.", m_sceneDesc.path.string());
        }

        static constexpr std::string_view kSceneScalePrefix = "--scene-scale=";
        if (arg.starts_with(kSceneScalePrefix))
        {
            const std::string_view scaleText = arg.substr(kSceneScalePrefix.size());
            float parsedScale = 0.0f;
            const char* begin = scaleText.data();
            const char* end = begin + scaleText.size();
            const auto [ptr, ec] = std::from_chars(begin, end, parsedScale);
            if (ec == std::errc{} && ptr == end && parsedScale > 0.0f)
            {
                m_sceneDesc.uniformScale = parsedScale;
                WEST_LOG_INFO(LogCategory::Core, "Scene scale set to {}.", m_sceneDesc.uniformScale);
            }
            else
            {
                WEST_LOG_WARNING(LogCategory::Core, "Ignoring invalid scene scale argument: {}", arg);
            }
        }

        static constexpr std::string_view kTextureMaxDimensionPrefix = "--texture-max-dimension=";
        if (arg.starts_with(kTextureMaxDimensionPrefix))
        {
            const std::string_view dimensionText = arg.substr(kTextureMaxDimensionPrefix.size());
            uint32 parsedMaxDimension = 0;
            const char* begin = dimensionText.data();
            const char* end = begin + dimensionText.size();
            const auto [ptr, ec] = std::from_chars(begin, end, parsedMaxDimension);
            if (ec == std::errc{} && ptr == end)
            {
                m_sceneTextureMaxDimension = parsedMaxDimension;
                WEST_LOG_INFO(LogCategory::Core, "Scene texture max dimension set to {}.", m_sceneTextureMaxDimension);
            }
            else
            {
                WEST_LOG_WARNING(LogCategory::Core, "Ignoring invalid texture max dimension argument: {}", arg);
            }
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
                            "frameLimit={}, scenePreset={}, sceneCache={}, sceneMerge={}, sceneBatchUpload={}, "
                            "textureCache={}, textureBatchUpload={}, textureMaxDimension={}, gpuDrivenScene={}",
                            BuildConfigName(), BackendName(m_backend), OnOff(m_enableValidation),
                            OnOff(m_enableDX12GPUBasedValidation), OnOff(m_enableGPUCrashDiag), m_maxFrameCount,
                            SceneDisplayName(m_sceneDesc), OnOff(m_enableSceneCache),
                            OnOff(m_enableSceneMerge), OnOff(m_enableSceneBatchUpload), OnOff(m_enableTextureCache),
                            OnOff(m_enableTextureBatchUpload), m_sceneTextureMaxDimension,
                            OnOff(m_enableGPUDrivenScene)));

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
    config.enableGPUBasedValidation = m_enableDX12GPUBasedValidation;
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
                    std::format("RHI Backend: {} (validation={}, dx12GBV={}, gpuCrashDiag={})", BackendName(m_backend),
                                OnOff(config.enableValidation), OnOff(config.enableGPUBasedValidation),
                                OnOff(config.enableGPUCrashDiag)));
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

    rhi::RHISamplerDesc materialStableSamplerDesc{};
    materialStableSamplerDesc.minFilter = rhi::RHIFilter::Linear;
    materialStableSamplerDesc.magFilter = rhi::RHIFilter::Linear;
    materialStableSamplerDesc.mipmapMode = rhi::RHIMipmapMode::Linear;
    materialStableSamplerDesc.addressU = rhi::RHIAddressMode::Repeat;
    materialStableSamplerDesc.addressV = rhi::RHIAddressMode::Repeat;
    materialStableSamplerDesc.addressW = rhi::RHIAddressMode::Repeat;
    materialStableSamplerDesc.anisotropyEnable = true;
    materialStableSamplerDesc.maxAnisotropy = 16.0f;
    materialStableSamplerDesc.debugName = "MaterialStableSampler";
    m_materialStableSampler = m_rhiDevice->CreateSampler(materialStableSamplerDesc);
    WEST_CHECK(m_materialStableSampler != nullptr, "Failed to create stable material sampler");
    WEST_CHECK(m_rhiDevice->RegisterBindlessResource(m_materialStableSampler.get()) != rhi::kInvalidBindlessIndex,
               "Failed to register stable material sampler");

    rhi::RHISamplerDesc shadowSamplerDesc{};
    shadowSamplerDesc.minFilter = rhi::RHIFilter::Nearest;
    shadowSamplerDesc.magFilter = rhi::RHIFilter::Nearest;
    shadowSamplerDesc.mipmapMode = rhi::RHIMipmapMode::Nearest;
    shadowSamplerDesc.addressU = rhi::RHIAddressMode::ClampToBorder;
    shadowSamplerDesc.addressV = rhi::RHIAddressMode::ClampToBorder;
    shadowSamplerDesc.addressW = rhi::RHIAddressMode::ClampToBorder;
    shadowSamplerDesc.anisotropyEnable = false;
    shadowSamplerDesc.borderColor = rhi::RHIBorderColor::OpaqueWhite;
    shadowSamplerDesc.debugName = "ShadowMapSampler";
    m_shadowSampler = m_rhiDevice->CreateSampler(shadowSamplerDesc);
    WEST_CHECK(m_shadowSampler != nullptr, "Failed to create shadow sampler");
    WEST_CHECK(m_rhiDevice->RegisterBindlessResource(m_shadowSampler.get()) != rhi::kInvalidBindlessIndex,
               "Failed to register shadow sampler");

    rhi::RHISamplerDesc iblSamplerDesc{};
    iblSamplerDesc.minFilter = rhi::RHIFilter::Linear;
    iblSamplerDesc.magFilter = rhi::RHIFilter::Linear;
    iblSamplerDesc.mipmapMode = rhi::RHIMipmapMode::Linear;
    iblSamplerDesc.addressU = rhi::RHIAddressMode::ClampToEdge;
    iblSamplerDesc.addressV = rhi::RHIAddressMode::ClampToEdge;
    iblSamplerDesc.addressW = rhi::RHIAddressMode::ClampToEdge;
    iblSamplerDesc.anisotropyEnable = false;
    iblSamplerDesc.debugName = "IBLSampler";
    m_iblSampler = m_rhiDevice->CreateSampler(iblSamplerDesc);
    WEST_CHECK(m_iblSampler != nullptr, "Failed to create IBL sampler");
    WEST_CHECK(m_rhiDevice->RegisterBindlessResource(m_iblSampler.get()) != rhi::kInvalidBindlessIndex,
               "Failed to register IBL sampler");

    InitializeScene();
    m_commandListPool = std::make_unique<render::CommandListPool>();
    m_frameGraph = std::make_unique<render::RenderGraph>();
    m_transientResourcePool = std::make_unique<render::TransientResourcePool>();
    m_shadowMapPass =
        std::make_unique<render::ShadowMapPass>(*m_rhiDevice, *m_psoCache, m_backend, m_materialStableSampler.get());
    m_gBufferPass =
        std::make_unique<render::GBufferPass>(*m_rhiDevice, *m_psoCache, m_backend, m_materialStableSampler.get());
    if (m_gpuDrivenAvailable)
    {
        m_gpuDrivenCullingPass = std::make_unique<render::GPUDrivenCullingPass>(*m_rhiDevice, *m_psoCache, m_backend);
        m_gpuDrivenCountResetPass = std::make_unique<render::BufferCopyPass>("GPUDrivenCountResetPass");
        m_gpuDrivenCountReadbackPass = std::make_unique<render::BufferCopyPass>("GPUDrivenCountReadbackPass");
    }
    m_ssaoPass = std::make_unique<render::SSAOPass>(*m_rhiDevice, *m_psoCache, m_backend);
    m_deferredLightingPass = std::make_unique<render::DeferredLightingPass>(
        *m_rhiDevice, *m_psoCache, m_backend, m_checkerSampler.get(), m_shadowSampler.get(), m_iblSampler.get());
    m_bokehDOFPass = std::make_unique<render::BokehDOFPass>(*m_rhiDevice, *m_psoCache, m_backend, m_iblSampler.get());
    m_toneMappingPass =
        std::make_unique<render::ToneMappingPass>(*m_rhiDevice, *m_psoCache, m_backend, m_checkerSampler.get());
    InitializeImGui();
    ApplyPostPreset(0, false);
    if (m_maxFrameCount == 0)
    {
        LogRuntimePostControlsHelp();
        LogRuntimePostState("Initial runtime post state");
    }
    RunCommandRecordingBenchmark();
}

void Win32Application::InitializeImGui()
{
    WEST_ASSERT(m_rhiDevice != nullptr);
    WEST_ASSERT(m_psoCache != nullptr);

    m_frameTelemetry = std::make_unique<editor::FrameTelemetry>();
    m_gpuTimerManager = std::make_unique<editor::GPUTimerManager>();
    m_gpuTimerManager->Initialize(*m_rhiDevice, kMaxFramesInFlight);
    m_imguiRenderer = std::make_unique<editor::ImGuiRenderer>(*m_rhiDevice, *m_psoCache, m_backend, kMaxFramesInFlight);
    m_imguiPass = std::make_unique<editor::ImGuiPass>(*m_imguiRenderer);
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
        {{-0.65f, 0.65f, 0.0f}, {0.0f, 0.0f}},
        {{0.65f, 0.65f, 0.0f}, {1.0f, 0.0f}},
        {{0.65f, -0.65f, 0.0f}, {1.0f, 1.0f}},
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
    copySubmit.commandLists = std::span<rhi::IRHICommandList* const>(copyCommandLists.data(), copyCommandLists.size());
    copySubmit.timelineSignals = std::span<const rhi::RHITimelineSignalDesc>(copySignals.data(), copySignals.size());

    auto* queue = m_rhiDevice->GetQueue(rhi::RHIQueueType::Graphics);
    queue->Submit(copySubmit);
    copyFence->Wait(1);

    WEST_LOG_INFO(LogCategory::RHI, "Textured quad resources uploaded (VB={} bytes, IB={} bytes, texture={}x{}).",
                  vbSize, ibSize, kTextureWidth, kTextureHeight);
    WEST_LOG_INFO(LogCategory::RHI, "Bindless textured quad resources registered (texture={}, sampler={}).",
                  textureIndex, samplerIndex);
}

void Win32Application::InitializeScene()
{
    WEST_PROFILE_FUNCTION();

    WEST_ASSERT(m_rhiDevice != nullptr);
    WEST_ASSERT(m_checkerSampler != nullptr);

    using Clock = std::chrono::high_resolution_clock;

    const auto sceneInitStart = Clock::now();
    scene::SceneLoadOptions loadOptions{};
    loadOptions.uniformScale = m_sceneDesc.uniformScale;
    loadOptions.enableCache = m_enableSceneCache;
    loadOptions.enableStaticMeshMerge = m_enableSceneMerge;

    if (m_sceneDesc.kind == ApplicationSceneKind::StaticScene)
    {
        const std::filesystem::path scenePath = ResolveScenePath(m_sceneDesc.path);
        Logger::Log(LogLevel::Info, LogCategory::Scene,
                    std::format("Loading scene {} from {}", SceneDisplayName(m_sceneDesc), scenePath.string()));
        m_sceneAsset =
            std::make_unique<scene::SceneAsset>(scene::SceneAssetLoader::LoadStaticScene(scenePath, loadOptions));
    }
    else
    {
        const std::filesystem::path bistroRoot =
            m_sceneDesc.path.empty() ? FindAmazonLumberyardBistroRoot() : ResolveScenePath(m_sceneDesc.path);
        Logger::Log(LogLevel::Info, LogCategory::Scene,
                    std::format("Loading scene {} from {}", SceneDisplayName(m_sceneDesc), bistroRoot.string()));
        m_sceneAsset = std::make_unique<scene::SceneAsset>(
            scene::SceneAssetLoader::LoadAmazonLumberyardBistro(bistroRoot, loadOptions));
    }

    m_sceneCamera = std::make_unique<scene::Camera>();
    m_sceneVertexBuffer.reset();
    m_sceneIndexBuffer.reset();
    m_sceneDrawBuffer.reset();
    m_gpuDrivenCountResetBuffer.reset();
    m_gpuDrivenIndirectArgsBuffers.clear();
    m_gpuDrivenIndirectCountBuffers.clear();
    m_gpuDrivenCountReadbackBuffers.clear();
    m_gpuDrivenReadbackPending.clear();
    m_sceneMeshResources.clear();
    m_sceneTextureResources.clear();
    m_iblPrefilteredTexture.reset();
    m_iblIrradianceTexture.reset();
    m_iblBrdfLutTexture.reset();
    m_sceneDrawCount = 0;
    m_lastGPUDrivenVisibleCount = 0;
    m_gpuDrivenAvailable = false;
    m_gpuDrivenVisibilityLogged = false;

    const scene::SceneLoadStats& loadStats = m_sceneAsset->GetLoadStats();
    const auto& sceneMeshes = m_sceneAsset->GetMeshes();
    const auto& sceneTextures = m_sceneAsset->GetTextures();
    std::vector<std::unique_ptr<rhi::IRHIBuffer>> stagingBuffers;
    stagingBuffers.reserve(std::max<size_t>(2, sceneTextures.size() + 2));

    auto uploadCommandList = m_rhiDevice->CreateCommandList(rhi::RHIQueueType::Graphics);
    uploadCommandList->Begin();

    const auto geometryUploadStart = Clock::now();

    if (m_enableSceneBatchUpload)
    {
        uint64 totalVertexBytes = 0;
        uint64 totalIndexBytes = 0;
        m_sceneMeshResources.reserve(sceneMeshes.size());

        for (const scene::MeshData& mesh : sceneMeshes)
        {
            SceneMeshResource meshResource{};
            meshResource.vertexOffsetBytes = totalVertexBytes;
            meshResource.indexOffsetBytes = totalIndexBytes;
            meshResource.indexCount = static_cast<uint32>(mesh.indices.size());
            meshResource.materialIndex = mesh.materialIndex;
            m_sceneMeshResources.push_back(std::move(meshResource));

            totalVertexBytes += static_cast<uint64>(mesh.vertices.size() * sizeof(scene::MeshVertex));
            totalIndexBytes += static_cast<uint64>(mesh.indices.size() * sizeof(uint32));
        }

        if (totalVertexBytes > 0)
        {
            rhi::RHIBufferDesc vertexStagingDesc{};
            vertexStagingDesc.sizeBytes = totalVertexBytes;
            vertexStagingDesc.structureByteStride = sizeof(scene::MeshVertex);
            vertexStagingDesc.usage = rhi::RHIBufferUsage::CopySource;
            vertexStagingDesc.memoryType = rhi::RHIMemoryType::Upload;
            vertexStagingDesc.debugName = "SceneVertexStaging";

            auto vertexStaging = m_rhiDevice->CreateBuffer(vertexStagingDesc);
            WEST_CHECK(vertexStaging != nullptr, "Failed to create batched vertex staging buffer");
            auto* mappedVertexData = static_cast<uint8_t*>(vertexStaging->Map());
            WEST_CHECK(mappedVertexData != nullptr, "Failed to map batched vertex staging buffer");

            for (size_t meshIndex = 0; meshIndex < sceneMeshes.size(); ++meshIndex)
            {
                const scene::MeshData& mesh = sceneMeshes[meshIndex];
                const uint64 vertexBytes = static_cast<uint64>(mesh.vertices.size() * sizeof(scene::MeshVertex));
                if (vertexBytes == 0)
                {
                    continue;
                }

                std::memcpy(mappedVertexData + m_sceneMeshResources[meshIndex].vertexOffsetBytes, mesh.vertices.data(),
                            static_cast<size_t>(vertexBytes));
            }
            vertexStaging->Unmap();

            rhi::RHIBufferDesc vertexBufferDesc{};
            vertexBufferDesc.sizeBytes = totalVertexBytes;
            vertexBufferDesc.structureByteStride = sizeof(scene::MeshVertex);
            vertexBufferDesc.usage = rhi::RHIBufferUsage::VertexBuffer | rhi::RHIBufferUsage::CopyDest;
            vertexBufferDesc.memoryType = rhi::RHIMemoryType::GPULocal;
            vertexBufferDesc.debugName = "SceneVertexBuffer";

            m_sceneVertexBuffer = m_rhiDevice->CreateBuffer(vertexBufferDesc);
            WEST_CHECK(m_sceneVertexBuffer != nullptr, "Failed to create batched vertex buffer");

            uploadCommandList->CopyBuffer(vertexStaging.get(), 0, m_sceneVertexBuffer.get(), 0, totalVertexBytes);

            rhi::RHIBarrierDesc vertexBarrier{};
            vertexBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
            vertexBarrier.buffer = m_sceneVertexBuffer.get();
            vertexBarrier.stateBefore = rhi::RHIResourceState::CopyDest;
            vertexBarrier.stateAfter = rhi::RHIResourceState::VertexBuffer;
            uploadCommandList->ResourceBarrier(vertexBarrier);

            stagingBuffers.push_back(std::move(vertexStaging));
        }

        if (totalIndexBytes > 0)
        {
            rhi::RHIBufferDesc indexStagingDesc{};
            indexStagingDesc.sizeBytes = totalIndexBytes;
            indexStagingDesc.structureByteStride = sizeof(uint32);
            indexStagingDesc.usage = rhi::RHIBufferUsage::CopySource;
            indexStagingDesc.memoryType = rhi::RHIMemoryType::Upload;
            indexStagingDesc.debugName = "SceneIndexStaging";

            auto indexStaging = m_rhiDevice->CreateBuffer(indexStagingDesc);
            WEST_CHECK(indexStaging != nullptr, "Failed to create batched index staging buffer");
            auto* mappedIndexData = static_cast<uint8_t*>(indexStaging->Map());
            WEST_CHECK(mappedIndexData != nullptr, "Failed to map batched index staging buffer");

            for (size_t meshIndex = 0; meshIndex < sceneMeshes.size(); ++meshIndex)
            {
                const scene::MeshData& mesh = sceneMeshes[meshIndex];
                const uint64 indexBytes = static_cast<uint64>(mesh.indices.size() * sizeof(uint32));
                if (indexBytes == 0)
                {
                    continue;
                }

                std::memcpy(mappedIndexData + m_sceneMeshResources[meshIndex].indexOffsetBytes, mesh.indices.data(),
                            static_cast<size_t>(indexBytes));
            }
            indexStaging->Unmap();

            rhi::RHIBufferDesc indexBufferDesc{};
            indexBufferDesc.sizeBytes = totalIndexBytes;
            indexBufferDesc.structureByteStride = sizeof(uint32);
            indexBufferDesc.usage = rhi::RHIBufferUsage::IndexBuffer | rhi::RHIBufferUsage::CopyDest;
            indexBufferDesc.memoryType = rhi::RHIMemoryType::GPULocal;
            indexBufferDesc.debugName = "SceneIndexBuffer";

            m_sceneIndexBuffer = m_rhiDevice->CreateBuffer(indexBufferDesc);
            WEST_CHECK(m_sceneIndexBuffer != nullptr, "Failed to create batched index buffer");

            uploadCommandList->CopyBuffer(indexStaging.get(), 0, m_sceneIndexBuffer.get(), 0, totalIndexBytes);

            rhi::RHIBarrierDesc indexBarrier{};
            indexBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
            indexBarrier.buffer = m_sceneIndexBuffer.get();
            indexBarrier.stateBefore = rhi::RHIResourceState::CopyDest;
            indexBarrier.stateAfter = rhi::RHIResourceState::IndexBuffer;
            uploadCommandList->ResourceBarrier(indexBarrier);

            stagingBuffers.push_back(std::move(indexStaging));
        }

        for (SceneMeshResource& meshResource : m_sceneMeshResources)
        {
            meshResource.vertexBuffer = m_sceneVertexBuffer.get();
            meshResource.indexBuffer = m_sceneIndexBuffer.get();
        }
    }
    else
    {
        m_sceneMeshResources.reserve(sceneMeshes.size());
        for (const scene::MeshData& mesh : sceneMeshes)
        {
            const uint64 vertexBytes = static_cast<uint64>(mesh.vertices.size() * sizeof(scene::MeshVertex));
            const uint64 indexBytes = static_cast<uint64>(mesh.indices.size() * sizeof(uint32));

            rhi::RHIBufferDesc vertexStagingDesc{};
            vertexStagingDesc.sizeBytes = vertexBytes;
            vertexStagingDesc.structureByteStride = sizeof(scene::MeshVertex);
            vertexStagingDesc.usage = rhi::RHIBufferUsage::CopySource;
            vertexStagingDesc.memoryType = rhi::RHIMemoryType::Upload;
            vertexStagingDesc.debugName = "VertexStaging";

            auto vertexStaging = m_rhiDevice->CreateBuffer(vertexStagingDesc);
            WEST_CHECK(vertexStaging != nullptr, "Failed to create vertex staging buffer");
            void* mapped = vertexStaging->Map();
            WEST_CHECK(mapped != nullptr, "Failed to map vertex staging buffer");
            std::memcpy(mapped, mesh.vertices.data(), static_cast<size_t>(vertexBytes));
            vertexStaging->Unmap();

            rhi::RHIBufferDesc indexStagingDesc{};
            indexStagingDesc.sizeBytes = indexBytes;
            indexStagingDesc.structureByteStride = sizeof(uint32);
            indexStagingDesc.usage = rhi::RHIBufferUsage::CopySource;
            indexStagingDesc.memoryType = rhi::RHIMemoryType::Upload;
            indexStagingDesc.debugName = "IndexStaging";

            auto indexStaging = m_rhiDevice->CreateBuffer(indexStagingDesc);
            WEST_CHECK(indexStaging != nullptr, "Failed to create index staging buffer");
            mapped = indexStaging->Map();
            WEST_CHECK(mapped != nullptr, "Failed to map index staging buffer");
            std::memcpy(mapped, mesh.indices.data(), static_cast<size_t>(indexBytes));
            indexStaging->Unmap();

            rhi::RHIBufferDesc vertexBufferDesc{};
            vertexBufferDesc.sizeBytes = vertexBytes;
            vertexBufferDesc.structureByteStride = sizeof(scene::MeshVertex);
            vertexBufferDesc.usage = rhi::RHIBufferUsage::VertexBuffer | rhi::RHIBufferUsage::CopyDest;
            vertexBufferDesc.memoryType = rhi::RHIMemoryType::GPULocal;
            vertexBufferDesc.debugName = mesh.debugName.c_str();

            auto vertexBuffer = m_rhiDevice->CreateBuffer(vertexBufferDesc);
            WEST_CHECK(vertexBuffer != nullptr, "Failed to create vertex buffer");

            rhi::RHIBufferDesc indexBufferDesc{};
            indexBufferDesc.sizeBytes = indexBytes;
            indexBufferDesc.structureByteStride = sizeof(uint32);
            indexBufferDesc.usage = rhi::RHIBufferUsage::IndexBuffer | rhi::RHIBufferUsage::CopyDest;
            indexBufferDesc.memoryType = rhi::RHIMemoryType::GPULocal;
            indexBufferDesc.debugName = mesh.debugName.c_str();

            auto indexBuffer = m_rhiDevice->CreateBuffer(indexBufferDesc);
            WEST_CHECK(indexBuffer != nullptr, "Failed to create index buffer");

            uploadCommandList->CopyBuffer(vertexStaging.get(), 0, vertexBuffer.get(), 0, vertexBytes);
            uploadCommandList->CopyBuffer(indexStaging.get(), 0, indexBuffer.get(), 0, indexBytes);

            rhi::RHIBarrierDesc vertexBarrier{};
            vertexBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
            vertexBarrier.buffer = vertexBuffer.get();
            vertexBarrier.stateBefore = rhi::RHIResourceState::CopyDest;
            vertexBarrier.stateAfter = rhi::RHIResourceState::VertexBuffer;
            uploadCommandList->ResourceBarrier(vertexBarrier);

            rhi::RHIBarrierDesc indexBarrier{};
            indexBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
            indexBarrier.buffer = indexBuffer.get();
            indexBarrier.stateBefore = rhi::RHIResourceState::CopyDest;
            indexBarrier.stateAfter = rhi::RHIResourceState::IndexBuffer;
            uploadCommandList->ResourceBarrier(indexBarrier);

            SceneMeshResource meshResource{};
            meshResource.ownedVertexBuffer = std::move(vertexBuffer);
            meshResource.ownedIndexBuffer = std::move(indexBuffer);
            meshResource.vertexBuffer = meshResource.ownedVertexBuffer.get();
            meshResource.indexBuffer = meshResource.ownedIndexBuffer.get();
            meshResource.indexCount = static_cast<uint32>(mesh.indices.size());
            meshResource.materialIndex = mesh.materialIndex;
            m_sceneMeshResources.push_back(std::move(meshResource));

            stagingBuffers.push_back(std::move(vertexStaging));
            stagingBuffers.push_back(std::move(indexStaging));
        }
    }

    const double geometryUploadMs =
        std::chrono::duration<double, std::milli>(Clock::now() - geometryUploadStart).count();

    struct LoadedSceneTexture
    {
        const scene::TextureAsset* asset = nullptr;
        scene::TextureAssetData textureData;
        scene::TextureAssetLoadStats loadStats;
        uint64_t stagingBytes = 0;
        bool usedFallback = false;

        struct StagedSubresource
        {
            uint32_t subresourceIndex = 0;
            uint32_t alignedRowPitchBytes = 0;
            uint64_t stagingOffsetBytes = 0;
            uint64_t stagingBytes = 0;
        };
        std::vector<StagedSubresource> stagedSubresources;
    };

    struct TextureLoadSummary
    {
        uint32_t cacheHits = 0;
        uint32_t cacheMisses = 0;
        uint32_t cacheWrites = 0;
        uint32_t fallbackCount = 0;
        double cacheReadMs = 0.0;
        double sourceImageCacheReadMs = 0.0;
        double decodeMs = 0.0;
        double mipBuildMs = 0.0;
        double cacheWriteMs = 0.0;
    };

    const auto textureLoadStart = Clock::now();
    std::vector<LoadedSceneTexture> loadedTextures;
    loadedTextures.reserve(sceneTextures.size());
    TextureLoadSummary textureLoadSummary{};
    uint64_t totalTextureStagingBytes = 0;

    for (const scene::TextureAsset& textureAsset : sceneTextures)
    {
        LoadedSceneTexture loadedTexture{};
        loadedTexture.asset = &textureAsset;

        scene::TextureAssetLoadOptions textureLoadOptions{};
        textureLoadOptions.enableCache = m_enableTextureCache;
        textureLoadOptions.generateMipChain = true;
        textureLoadOptions.maxDimension = m_sceneTextureMaxDimension;
        if (auto loadedAsset = scene::LoadTexture2DAssetRGBA8WithStats(textureAsset.sourcePath, textureAsset.debugName,
                                                                       true, textureLoadOptions);
            loadedAsset.has_value())
        {
            loadedTexture.textureData = std::move(loadedAsset->texture);
            loadedTexture.loadStats = loadedAsset->stats;
            textureLoadSummary.cacheHits += loadedTexture.loadStats.usedCache ? 1u : 0u;
            textureLoadSummary.cacheMisses += loadedTexture.loadStats.usedCache ? 0u : 1u;
            textureLoadSummary.cacheWrites += loadedTexture.loadStats.cacheWritten ? 1u : 0u;
            textureLoadSummary.cacheReadMs += loadedTexture.loadStats.cacheReadMs;
            textureLoadSummary.sourceImageCacheReadMs += loadedTexture.loadStats.sourceImageCacheReadMs;
            textureLoadSummary.decodeMs += loadedTexture.loadStats.decodeMs;
            textureLoadSummary.mipBuildMs += loadedTexture.loadStats.mipBuildMs;
            textureLoadSummary.cacheWriteMs += loadedTexture.loadStats.cacheWriteMs;
        }
        else
        {
            scene::ImageData fallbackImage{};
            fallbackImage.width = 1;
            fallbackImage.height = 1;
            fallbackImage.pixelsRGBA8 = {255, 255, 255, 255};
            loadedTexture.textureData =
                scene::BuildTexture2DAssetRGBA8(textureAsset.debugName, std::move(fallbackImage), true);
            loadedTexture.usedFallback = true;
            textureLoadSummary.fallbackCount += 1;
        }

        loadedTexture.stagedSubresources.reserve(loadedTexture.textureData.subresources.size());
        uint64_t perTextureStagingBytes = 0;
        WEST_CHECK(rhi::GetFormatByteSize(loadedTexture.textureData.format) > 0,
                   "Unsupported scene texture upload format");
        WEST_CHECK(!loadedTexture.textureData.subresources.empty(), "Scene texture must have uploadable subresources");
        for (uint32_t subresourceIndex = 0;
             subresourceIndex < static_cast<uint32_t>(loadedTexture.textureData.subresources.size());
             ++subresourceIndex)
        {
            const scene::TextureSubresourceData& subresource = loadedTexture.textureData.subresources[subresourceIndex];
            LoadedSceneTexture::StagedSubresource stagedSubresource{};
            stagedSubresource.subresourceIndex = subresourceIndex;
            stagedSubresource.alignedRowPitchBytes = AlignUp(subresource.rowPitchBytes, 256u);
            stagedSubresource.stagingBytes =
                static_cast<uint64_t>(stagedSubresource.alignedRowPitchBytes) *
                GetTextureUploadRowCount(loadedTexture.textureData.format, subresource.height) * subresource.depth;

            uint64_t& stagingCursor = m_enableTextureBatchUpload ? totalTextureStagingBytes : perTextureStagingBytes;
            stagedSubresource.stagingOffsetBytes = AlignUp(stagingCursor, 512ull);
            stagingCursor = stagedSubresource.stagingOffsetBytes + stagedSubresource.stagingBytes;
            loadedTexture.stagedSubresources.push_back(stagedSubresource);
        }

        if (m_enableTextureBatchUpload)
        {
            loadedTexture.stagingBytes = 0;
            for (const LoadedSceneTexture::StagedSubresource& stagedSubresource : loadedTexture.stagedSubresources)
            {
                loadedTexture.stagingBytes += stagedSubresource.stagingBytes;
            }
        }
        else
        {
            loadedTexture.stagingBytes = perTextureStagingBytes;
            totalTextureStagingBytes += loadedTexture.stagingBytes;
        }

        loadedTextures.push_back(std::move(loadedTexture));
    }

    const double textureLoadMs = std::chrono::duration<double, std::milli>(Clock::now() - textureLoadStart).count();
    const auto textureUploadStart = Clock::now();

    m_sceneTextureResources.reserve(sceneTextures.size());

    if (m_enableTextureBatchUpload && totalTextureStagingBytes > 0)
    {
        rhi::RHIBufferDesc textureStagingDesc{};
        textureStagingDesc.sizeBytes = totalTextureStagingBytes;
        textureStagingDesc.usage = rhi::RHIBufferUsage::CopySource;
        textureStagingDesc.memoryType = rhi::RHIMemoryType::Upload;
        textureStagingDesc.debugName = "SceneTextureBatchStaging";

        auto textureStaging = m_rhiDevice->CreateBuffer(textureStagingDesc);
        WEST_CHECK(textureStaging != nullptr, "Failed to create batched scene texture staging buffer");
        auto* mappedTexture = static_cast<uint8_t*>(textureStaging->Map());
        WEST_CHECK(mappedTexture != nullptr, "Failed to map batched scene texture staging buffer");
        std::memset(mappedTexture, 0, static_cast<size_t>(totalTextureStagingBytes));

        for (const LoadedSceneTexture& loadedTexture : loadedTextures)
        {
            for (const LoadedSceneTexture::StagedSubresource& stagedSubresource : loadedTexture.stagedSubresources)
            {
                const scene::TextureSubresourceData& subresource =
                    loadedTexture.textureData.subresources[stagedSubresource.subresourceIndex];
                const uint32_t rowCount =
                    GetTextureUploadRowCount(loadedTexture.textureData.format, subresource.height);
                for (uint32_t row = 0; row < rowCount; ++row)
                {
                    const uint8_t* sourceRow = loadedTexture.textureData.bytes.data() + subresource.sourceOffsetBytes +
                                               (static_cast<size_t>(row) * subresource.rowPitchBytes);
                    uint8_t* destinationRow = mappedTexture + stagedSubresource.stagingOffsetBytes +
                                              (static_cast<size_t>(row) * stagedSubresource.alignedRowPitchBytes);
                    std::memcpy(destinationRow, sourceRow, subresource.rowPitchBytes);
                }
            }
        }
        textureStaging->Unmap();

        for (const LoadedSceneTexture& loadedTexture : loadedTextures)
        {
            const scene::TextureAssetData& textureData = loadedTexture.textureData;
            rhi::RHITextureDesc textureDesc{};
            textureDesc.width = textureData.width;
            textureDesc.height = textureData.height;
            textureDesc.depth = textureData.depth;
            textureDesc.mipLevels = textureData.mipLevels;
            textureDesc.arrayLayers = textureData.arrayLayers;
            textureDesc.format = textureData.format;
            textureDesc.dimension = textureData.dimension;
            textureDesc.usage = rhi::RHITextureUsage::ShaderResource | rhi::RHITextureUsage::CopyDest;
            textureDesc.debugName = loadedTexture.asset->debugName.c_str();

            auto texture = m_rhiDevice->CreateTexture(textureDesc);
            WEST_CHECK(texture != nullptr, "Failed to create scene texture");

            rhi::RHIBarrierDesc textureToCopyBarrier{};
            textureToCopyBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
            textureToCopyBarrier.texture = texture.get();
            textureToCopyBarrier.stateBefore = rhi::RHIResourceState::Undefined;
            textureToCopyBarrier.stateAfter = rhi::RHIResourceState::CopyDest;
            uploadCommandList->ResourceBarrier(textureToCopyBarrier);

            WEST_CHECK(rhi::GetFormatByteSize(textureData.format) > 0, "Unsupported scene texture upload format");
            for (const LoadedSceneTexture::StagedSubresource& stagedSubresource : loadedTexture.stagedSubresources)
            {
                const scene::TextureSubresourceData& subresource =
                    textureData.subresources[stagedSubresource.subresourceIndex];

                rhi::RHICopyRegion copyRegion{};
                copyRegion.bufferOffset = stagedSubresource.stagingOffsetBytes;
                copyRegion.bufferRowLength =
                    GetTextureUploadRowLengthTexels(textureData.format, stagedSubresource.alignedRowPitchBytes);
                copyRegion.bufferImageHeight = subresource.height;
                copyRegion.texWidth = subresource.width;
                copyRegion.texHeight = subresource.height;
                copyRegion.texDepth = subresource.depth;
                copyRegion.mipLevel = subresource.mipLevel;
                copyRegion.arrayLayer = subresource.arrayLayer;
                uploadCommandList->CopyBufferToTexture(textureStaging.get(), texture.get(), copyRegion);
            }

            rhi::RHIBarrierDesc textureBarrier{};
            textureBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
            textureBarrier.texture = texture.get();
            textureBarrier.stateBefore = rhi::RHIResourceState::CopyDest;
            textureBarrier.stateAfter = rhi::RHIResourceState::ShaderResource;
            uploadCommandList->ResourceBarrier(textureBarrier);

            WEST_CHECK(m_rhiDevice->RegisterBindlessResource(texture.get()) != rhi::kInvalidBindlessIndex,
                       "Failed to register scene texture");

            SceneTextureResource textureResource{};
            textureResource.sourcePath = loadedTexture.asset->sourcePath;
            textureResource.texture = std::move(texture);
            m_sceneTextureResources.push_back(std::move(textureResource));
        }

        stagingBuffers.push_back(std::move(textureStaging));
    }
    else
    {
        for (const LoadedSceneTexture& loadedTexture : loadedTextures)
        {
            rhi::RHIBufferDesc textureStagingDesc{};
            textureStagingDesc.sizeBytes = loadedTexture.stagingBytes;
            textureStagingDesc.usage = rhi::RHIBufferUsage::CopySource;
            textureStagingDesc.memoryType = rhi::RHIMemoryType::Upload;
            textureStagingDesc.debugName = "SceneTextureStaging";

            auto textureStaging = m_rhiDevice->CreateBuffer(textureStagingDesc);
            WEST_CHECK(textureStaging != nullptr, "Failed to create scene texture staging buffer");
            auto* mappedTexture = static_cast<uint8_t*>(textureStaging->Map());
            WEST_CHECK(mappedTexture != nullptr, "Failed to map scene texture staging buffer");
            std::memset(mappedTexture, 0, static_cast<size_t>(loadedTexture.stagingBytes));
            for (const LoadedSceneTexture::StagedSubresource& stagedSubresource : loadedTexture.stagedSubresources)
            {
                const scene::TextureSubresourceData& subresource =
                    loadedTexture.textureData.subresources[stagedSubresource.subresourceIndex];
                const uint32_t rowCount =
                    GetTextureUploadRowCount(loadedTexture.textureData.format, subresource.height);
                for (uint32_t row = 0; row < rowCount; ++row)
                {
                    const uint8_t* sourceRow = loadedTexture.textureData.bytes.data() + subresource.sourceOffsetBytes +
                                               (static_cast<size_t>(row) * subresource.rowPitchBytes);
                    uint8_t* destinationRow = mappedTexture + stagedSubresource.stagingOffsetBytes +
                                              (static_cast<size_t>(row) * stagedSubresource.alignedRowPitchBytes);
                    std::memcpy(destinationRow, sourceRow, subresource.rowPitchBytes);
                }
            }
            textureStaging->Unmap();

            const scene::TextureAssetData& textureData = loadedTexture.textureData;
            rhi::RHITextureDesc textureDesc{};
            textureDesc.width = textureData.width;
            textureDesc.height = textureData.height;
            textureDesc.depth = textureData.depth;
            textureDesc.mipLevels = textureData.mipLevels;
            textureDesc.arrayLayers = textureData.arrayLayers;
            textureDesc.format = textureData.format;
            textureDesc.dimension = textureData.dimension;
            textureDesc.usage = rhi::RHITextureUsage::ShaderResource | rhi::RHITextureUsage::CopyDest;
            textureDesc.debugName = loadedTexture.asset->debugName.c_str();

            auto texture = m_rhiDevice->CreateTexture(textureDesc);
            WEST_CHECK(texture != nullptr, "Failed to create scene texture");

            rhi::RHIBarrierDesc textureToCopyBarrier{};
            textureToCopyBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
            textureToCopyBarrier.texture = texture.get();
            textureToCopyBarrier.stateBefore = rhi::RHIResourceState::Undefined;
            textureToCopyBarrier.stateAfter = rhi::RHIResourceState::CopyDest;
            uploadCommandList->ResourceBarrier(textureToCopyBarrier);

            WEST_CHECK(rhi::GetFormatByteSize(textureData.format) > 0, "Unsupported scene texture upload format");
            for (const LoadedSceneTexture::StagedSubresource& stagedSubresource : loadedTexture.stagedSubresources)
            {
                const scene::TextureSubresourceData& subresource =
                    textureData.subresources[stagedSubresource.subresourceIndex];

                rhi::RHICopyRegion copyRegion{};
                copyRegion.bufferOffset = stagedSubresource.stagingOffsetBytes;
                copyRegion.bufferRowLength =
                    GetTextureUploadRowLengthTexels(textureData.format, stagedSubresource.alignedRowPitchBytes);
                copyRegion.bufferImageHeight = subresource.height;
                copyRegion.texWidth = subresource.width;
                copyRegion.texHeight = subresource.height;
                copyRegion.texDepth = subresource.depth;
                copyRegion.mipLevel = subresource.mipLevel;
                copyRegion.arrayLayer = subresource.arrayLayer;
                uploadCommandList->CopyBufferToTexture(textureStaging.get(), texture.get(), copyRegion);
            }

            rhi::RHIBarrierDesc textureBarrier{};
            textureBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
            textureBarrier.texture = texture.get();
            textureBarrier.stateBefore = rhi::RHIResourceState::CopyDest;
            textureBarrier.stateAfter = rhi::RHIResourceState::ShaderResource;
            uploadCommandList->ResourceBarrier(textureBarrier);

            WEST_CHECK(m_rhiDevice->RegisterBindlessResource(texture.get()) != rhi::kInvalidBindlessIndex,
                       "Failed to register scene texture");

            SceneTextureResource textureResource{};
            textureResource.sourcePath = loadedTexture.asset->sourcePath;
            textureResource.texture = std::move(texture);
            m_sceneTextureResources.push_back(std::move(textureResource));
            stagingBuffers.push_back(std::move(textureStaging));
        }
    }

    InitializeImageBasedLighting(*uploadCommandList, stagingBuffers);

    uploadCommandList->End();

    const double textureUploadMs = std::chrono::duration<double, std::milli>(Clock::now() - textureUploadStart).count();
    const auto uploadSubmitStart = Clock::now();

    auto uploadFence = m_rhiDevice->CreateFence(0);
    std::vector<rhi::IRHICommandList*> uploadCommandLists = {uploadCommandList.get()};
    std::vector<rhi::RHITimelineSignalDesc> uploadSignals = {{uploadFence.get(), 1}};
    rhi::RHISubmitInfo uploadSubmit{};
    uploadSubmit.commandLists =
        std::span<rhi::IRHICommandList* const>(uploadCommandLists.data(), uploadCommandLists.size());
    uploadSubmit.timelineSignals =
        std::span<const rhi::RHITimelineSignalDesc>(uploadSignals.data(), uploadSignals.size());
    m_rhiDevice->GetQueue(rhi::RHIQueueType::Graphics)->Submit(uploadSubmit);
    uploadFence->Wait(1);

    const double uploadSubmitWaitMs =
        std::chrono::duration<double, std::milli>(Clock::now() - uploadSubmitStart).count();
    const auto& sceneInstances = m_sceneAsset->GetInstances();

    std::vector<Bounds3> meshBounds(sceneMeshes.size());
    for (size_t meshIndex = 0; meshIndex < sceneMeshes.size(); ++meshIndex)
    {
        meshBounds[meshIndex] = ComputeMeshBounds(sceneMeshes[meshIndex]);
    }

    std::vector<render::GPUSceneDrawRecord> gpuDrawRecords;
    gpuDrawRecords.reserve(sceneInstances.size());
    for (const scene::InstanceData& instance : sceneInstances)
    {
        WEST_ASSERT(instance.meshIndex < sceneMeshes.size());
        WEST_ASSERT(instance.meshIndex < m_sceneMeshResources.size());

        const SceneMeshResource& meshResource = m_sceneMeshResources[instance.meshIndex];
        render::GPUSceneDrawRecord drawRecord{};
        drawRecord.materialIndex = meshResource.materialIndex;
        drawRecord.indexCount = meshResource.indexCount;
        drawRecord.firstIndex = static_cast<uint32_t>(meshResource.indexOffsetBytes / sizeof(uint32));
        drawRecord.vertexOffset = static_cast<int32_t>(meshResource.vertexOffsetBytes / sizeof(scene::MeshVertex));
        drawRecord.modelMatrix = instance.modelMatrix;
        const Bounds3 worldBounds = ComputeWorldBounds(meshBounds[instance.meshIndex], instance.modelMatrix);
        drawRecord.boundsSphere = ComputeBoundsSphere(worldBounds);
        drawRecord.boundsMin = {worldBounds.min[0], worldBounds.min[1], worldBounds.min[2], 0.0f};
        drawRecord.boundsMax = {worldBounds.max[0], worldBounds.max[1], worldBounds.max[2], 0.0f};
        gpuDrawRecords.push_back(drawRecord);
    }

    m_sceneDrawCount = static_cast<uint32_t>(gpuDrawRecords.size());
    if (!gpuDrawRecords.empty())
    {
        rhi::RHIBufferDesc drawBufferDesc{};
        drawBufferDesc.sizeBytes = static_cast<uint64_t>(gpuDrawRecords.size() * sizeof(render::GPUSceneDrawRecord));
        drawBufferDesc.structureByteStride = sizeof(render::GPUSceneDrawRecord);
        drawBufferDesc.usage = rhi::RHIBufferUsage::StorageBuffer;
        drawBufferDesc.memoryType = rhi::RHIMemoryType::Upload;
        drawBufferDesc.debugName = "SceneDrawBuffer";

        m_sceneDrawBuffer = m_rhiDevice->CreateBuffer(drawBufferDesc);
        WEST_CHECK(m_sceneDrawBuffer != nullptr, "Failed to create scene draw buffer");
        void* mappedDraws = m_sceneDrawBuffer->Map();
        WEST_CHECK(mappedDraws != nullptr, "Failed to map scene draw buffer");
        std::memcpy(mappedDraws, gpuDrawRecords.data(), gpuDrawRecords.size() * sizeof(render::GPUSceneDrawRecord));
        m_sceneDrawBuffer->Unmap();
        WEST_CHECK(m_rhiDevice->RegisterBindlessResource(m_sceneDrawBuffer.get()) != rhi::kInvalidBindlessIndex,
                   "Failed to register scene draw buffer");
    }

    const auto& sceneMaterials = m_sceneAsset->GetMaterials();
    std::vector<GPUMaterialData> gpuMaterials(sceneMaterials.size());
    for (size_t i = 0; i < sceneMaterials.size(); ++i)
    {
        const scene::Material& material = sceneMaterials[i];
        GPUMaterialData& gpuMaterial = gpuMaterials[i];
        gpuMaterial.baseColor[0] = material.baseColor[0];
        gpuMaterial.baseColor[1] = material.baseColor[1];
        gpuMaterial.baseColor[2] = material.baseColor[2];
        gpuMaterial.baseColor[3] = material.baseColor[3];
        gpuMaterial.roughness = material.roughness;
        gpuMaterial.metallic = material.metallic;
        if (IsBistroMetalMaterial(material.debugName))
        {
            gpuMaterial.metallic = 1.0f;
            gpuMaterial.roughness = 0.1f;
        }
        gpuMaterial.alphaCutoff = material.alphaCutoff;

        if (material.baseColorTextureIndex != scene::kInvalidSceneTextureIndex)
        {
            WEST_ASSERT(material.baseColorTextureIndex < m_sceneTextureResources.size());
            WEST_ASSERT(m_sceneTextureResources[material.baseColorTextureIndex].texture != nullptr);
            gpuMaterial.baseColorTexture.index =
                m_sceneTextureResources[material.baseColorTextureIndex].texture->GetBindlessIndex();
            gpuMaterial.hasBaseColorTexture = 1;
        }

        if (material.opacityTextureIndex != scene::kInvalidSceneTextureIndex)
        {
            WEST_ASSERT(material.opacityTextureIndex < m_sceneTextureResources.size());
            WEST_ASSERT(m_sceneTextureResources[material.opacityTextureIndex].texture != nullptr);
            gpuMaterial.opacityTexture.index =
                m_sceneTextureResources[material.opacityTextureIndex].texture->GetBindlessIndex();
            gpuMaterial.hasOpacityTexture = 1;
        }
    }

    rhi::RHIBufferDesc materialBufferDesc{};
    materialBufferDesc.sizeBytes = static_cast<uint64>(gpuMaterials.size() * sizeof(GPUMaterialData));
    materialBufferDesc.structureByteStride = sizeof(GPUMaterialData);
    materialBufferDesc.usage = rhi::RHIBufferUsage::StorageBuffer;
    materialBufferDesc.memoryType = rhi::RHIMemoryType::Upload;
    materialBufferDesc.debugName = "MaterialBuffer";

    m_materialBuffer = m_rhiDevice->CreateBuffer(materialBufferDesc);
    WEST_CHECK(m_materialBuffer != nullptr, "Failed to create material buffer");
    void* mapped = m_materialBuffer->Map();
    WEST_CHECK(mapped != nullptr, "Failed to map material buffer");
    std::memcpy(mapped, gpuMaterials.data(), gpuMaterials.size() * sizeof(GPUMaterialData));
    m_materialBuffer->Unmap();
    WEST_CHECK(m_rhiDevice->RegisterBindlessResource(m_materialBuffer.get()) != rhi::kInvalidBindlessIndex,
               "Failed to register material buffer");

    rhi::RHIBufferDesc frameBufferDesc{};
    frameBufferDesc.sizeBytes = sizeof(GPUFrameConstants);
    frameBufferDesc.structureByteStride = sizeof(GPUFrameConstants);
    frameBufferDesc.usage = rhi::RHIBufferUsage::StorageBuffer;
    frameBufferDesc.memoryType = rhi::RHIMemoryType::Upload;
    frameBufferDesc.debugName = "FrameBuffer";

    m_frameConstantsBuffers.clear();
    m_frameConstantsBuffers.reserve(kMaxFramesInFlight);
    for (uint32 frameSlot = 0; frameSlot < kMaxFramesInFlight; ++frameSlot)
    {
        auto frameConstantsBuffer = m_rhiDevice->CreateBuffer(frameBufferDesc);
        WEST_CHECK(frameConstantsBuffer != nullptr, "Failed to create frame buffer");
        WEST_CHECK(m_rhiDevice->RegisterBindlessResource(frameConstantsBuffer.get()) != rhi::kInvalidBindlessIndex,
                   "Failed to register frame buffer");
        m_frameConstantsBuffers.push_back(std::move(frameConstantsBuffer));
    }

    m_gpuDrivenAvailable = m_enableGPUDrivenScene && m_sceneDrawBuffer != nullptr && m_sceneDrawCount > 0 &&
                           m_sceneVertexBuffer != nullptr && m_sceneIndexBuffer != nullptr;

    if (m_gpuDrivenAvailable)
    {
        rhi::RHIBufferDesc countResetDesc{};
        countResetDesc.sizeBytes = sizeof(uint32_t);
        countResetDesc.structureByteStride = sizeof(uint32_t);
        countResetDesc.usage = rhi::RHIBufferUsage::CopySource;
        countResetDesc.memoryType = rhi::RHIMemoryType::Upload;
        countResetDesc.debugName = "GPUDrivenIndirectCountReset";

        m_gpuDrivenCountResetBuffer = m_rhiDevice->CreateBuffer(countResetDesc);
        WEST_CHECK(m_gpuDrivenCountResetBuffer != nullptr, "Failed to create GPU-driven count reset buffer");
        void* mappedCountReset = m_gpuDrivenCountResetBuffer->Map();
        WEST_CHECK(mappedCountReset != nullptr, "Failed to map GPU-driven count reset buffer");
        std::memset(mappedCountReset, 0, sizeof(uint32_t));
        m_gpuDrivenCountResetBuffer->Unmap();

        m_gpuDrivenIndirectArgsBuffers.reserve(kMaxFramesInFlight);
        m_gpuDrivenIndirectCountBuffers.reserve(kMaxFramesInFlight);
        m_gpuDrivenCountReadbackBuffers.reserve(kMaxFramesInFlight);
        m_gpuDrivenReadbackPending.assign(kMaxFramesInFlight, false);

        for (uint32_t frameSlot = 0; frameSlot < kMaxFramesInFlight; ++frameSlot)
        {
            rhi::RHIBufferDesc indirectArgsDesc{};
            indirectArgsDesc.sizeBytes =
                static_cast<uint64_t>(m_sceneDrawCount) * sizeof(render::DrawIndexedIndirectArgs);
            indirectArgsDesc.structureByteStride = sizeof(render::DrawIndexedIndirectArgs);
            indirectArgsDesc.usage = rhi::RHIBufferUsage::StorageBuffer | rhi::RHIBufferUsage::IndirectArgs;
            indirectArgsDesc.memoryType = rhi::RHIMemoryType::GPULocal;
            indirectArgsDesc.debugName = "GPUDrivenIndirectArgs";

            auto indirectArgsBuffer = m_rhiDevice->CreateBuffer(indirectArgsDesc);
            WEST_CHECK(indirectArgsBuffer != nullptr, "Failed to create GPU-driven indirect args buffer");
            WEST_CHECK(m_rhiDevice->RegisterBindlessResource(indirectArgsBuffer.get(),
                                                             rhi::RHIBindlessBufferView::ReadWrite) !=
                           rhi::kInvalidBindlessIndex,
                       "Failed to register GPU-driven indirect args buffer");
            m_gpuDrivenIndirectArgsBuffers.push_back(std::move(indirectArgsBuffer));

            rhi::RHIBufferDesc indirectCountDesc{};
            indirectCountDesc.sizeBytes = sizeof(uint32_t);
            indirectCountDesc.structureByteStride = sizeof(uint32_t);
            indirectCountDesc.usage = rhi::RHIBufferUsage::StorageBuffer | rhi::RHIBufferUsage::IndirectArgs |
                                      rhi::RHIBufferUsage::CopyDest | rhi::RHIBufferUsage::CopySource;
            indirectCountDesc.memoryType = rhi::RHIMemoryType::GPULocal;
            indirectCountDesc.debugName = "GPUDrivenIndirectCount";

            auto indirectCountBuffer = m_rhiDevice->CreateBuffer(indirectCountDesc);
            WEST_CHECK(indirectCountBuffer != nullptr, "Failed to create GPU-driven indirect count buffer");
            WEST_CHECK(m_rhiDevice->RegisterBindlessResource(indirectCountBuffer.get(),
                                                             rhi::RHIBindlessBufferView::ReadWrite) !=
                           rhi::kInvalidBindlessIndex,
                       "Failed to register GPU-driven indirect count buffer");
            m_gpuDrivenIndirectCountBuffers.push_back(std::move(indirectCountBuffer));

            rhi::RHIBufferDesc readbackDesc{};
            readbackDesc.sizeBytes = sizeof(uint32_t);
            readbackDesc.structureByteStride = sizeof(uint32_t);
            readbackDesc.usage = rhi::RHIBufferUsage::CopyDest;
            readbackDesc.memoryType = rhi::RHIMemoryType::Readback;
            readbackDesc.debugName = "GPUDrivenVisibleCountReadback";

            auto readbackBuffer = m_rhiDevice->CreateBuffer(readbackDesc);
            WEST_CHECK(readbackBuffer != nullptr, "Failed to create GPU-driven visible-count readback buffer");
            m_gpuDrivenCountReadbackBuffers.push_back(std::move(readbackBuffer));
        }

        Logger::Log(LogLevel::Info, LogCategory::Scene,
                    std::format("GPU-driven scene path enabled: draws={}, sharedGeometry={}, frameSlots={}.",
                                m_sceneDrawCount, OnOff(m_enableSceneBatchUpload), kMaxFramesInFlight));
    }
    else if (m_enableGPUDrivenScene)
    {
        Logger::Log(
            LogLevel::Info, LogCategory::Scene,
            std::format("GPU-driven scene path unavailable: draws={}, sceneBatchUpload={}, sharedVB={}, sharedIB={}.",
                        m_sceneDrawCount, OnOff(m_enableSceneBatchUpload),
                        m_sceneVertexBuffer != nullptr ? "yes" : "no", m_sceneIndexBuffer != nullptr ? "yes" : "no"));
    }

    InitializeFreeLookCamera();

    const double totalSceneInitMs = std::chrono::duration<double, std::milli>(Clock::now() - sceneInitStart).count();
    const std::string sceneName = m_sceneAsset->GetDebugName();

    Logger::Log(
        LogLevel::Info, LogCategory::Scene,
        std::format(
            "{} load stats: cacheUsed={}, cacheWritten={}, mergeApplied={}, meshes {}->{}, instances {}->{}, "
            "vertices {}->{}, indices {}->{}, load {:.2f} ms (cacheRead {:.2f}, import {:.2f}, optimize {:.2f}, "
            "cacheWrite {:.2f}).",
            sceneName, loadStats.usedCache, loadStats.cacheWritten, loadStats.appliedMeshMerge,
            loadStats.sourceMeshCount, loadStats.optimizedMeshCount, loadStats.sourceInstanceCount,
            loadStats.optimizedInstanceCount, loadStats.sourceVertexCount, loadStats.optimizedVertexCount,
            loadStats.sourceIndexCount, loadStats.optimizedIndexCount, loadStats.totalLoadMs, loadStats.cacheReadMs,
            loadStats.importMs, loadStats.optimizeMs, loadStats.cacheWriteMs));
    Logger::Log(
        LogLevel::Info, LogCategory::Scene,
        std::format("{} texture stats: cache={}, batchUpload={}, maxDimension={}, hits={}, misses={}, writes={}, "
                    "fallbacks={}, "
                    "load {:.2f} ms (assetCacheRead {:.2f}, sourceCacheRead {:.2f}, decode {:.2f}, "
                    "mipBuild {:.2f}, assetCacheWrite {:.2f}), upload {:.2f} ms, staging {:.2f} MiB.",
                    sceneName, OnOff(m_enableTextureCache), OnOff(m_enableTextureBatchUpload),
                    m_sceneTextureMaxDimension, textureLoadSummary.cacheHits, textureLoadSummary.cacheMisses,
                    textureLoadSummary.cacheWrites, textureLoadSummary.fallbackCount, textureLoadMs,
                    textureLoadSummary.cacheReadMs, textureLoadSummary.sourceImageCacheReadMs,
                    textureLoadSummary.decodeMs, textureLoadSummary.mipBuildMs, textureLoadSummary.cacheWriteMs,
                    textureUploadMs, static_cast<double>(totalTextureStagingBytes) / (1024.0 * 1024.0)));
    Logger::Log(
        LogLevel::Info, LogCategory::Scene,
        std::format("{} upload stats: geometryBatchUpload={}, textureBatchUpload={}, geometry {:.2f} ms, "
                    "submit/wait {:.2f} ms, total init {:.2f} ms (meshes={}, materials={}, textures={}, instances={}).",
                    sceneName, OnOff(m_enableSceneBatchUpload), OnOff(m_enableTextureBatchUpload), geometryUploadMs,
                    uploadSubmitWaitMs, totalSceneInitMs, sceneMeshes.size(), sceneMaterials.size(),
                    sceneTextures.size(), m_sceneAsset->GetInstances().size()));
}

void Win32Application::InitializeImageBasedLighting(rhi::IRHICommandList& uploadCommandList,
                                                    std::vector<std::unique_ptr<rhi::IRHIBuffer>>& stagingBuffers)
{
    WEST_ASSERT(m_rhiDevice != nullptr);
    WEST_ASSERT(m_iblSampler != nullptr);

    using Clock = std::chrono::high_resolution_clock;

    const std::filesystem::path iblRoot = FindGoldenGateHillsRoot();
    const std::filesystem::path prefilteredPath = iblRoot / "specularGgx.ktx2";
    const std::filesystem::path irradiancePath = iblRoot / "diffuseLambertian.ktx2";
    const std::filesystem::path brdfLutPath = iblRoot / "outputLUT.png";

    Logger::Log(LogLevel::Info, LogCategory::Scene, std::format("Loading IBL textures from {}", iblRoot.string()));

    const auto loadStart = Clock::now();
    std::optional<scene::TextureAssetData> prefilteredAsset = scene::LoadKtx2CubemapAsset(prefilteredPath);
    std::optional<scene::TextureAssetData> irradianceAsset = scene::LoadKtx2CubemapAsset(irradiancePath);

    scene::TextureAssetLoadOptions brdfLutOptions{};
    brdfLutOptions.enableCache = m_enableTextureCache;
    brdfLutOptions.generateMipChain = false;
    std::optional<scene::TextureAssetData> brdfLutAsset =
        scene::LoadTexture2DAssetRGBA8(brdfLutPath, false, brdfLutOptions);

    WEST_CHECK(prefilteredAsset.has_value(), "Failed to load prefiltered environment cubemap");
    WEST_CHECK(irradianceAsset.has_value(), "Failed to load irradiance environment cubemap");
    WEST_CHECK(brdfLutAsset.has_value(), "Failed to load BRDF LUT");
    const double loadMs = std::chrono::duration<double, std::milli>(Clock::now() - loadStart).count();

    const auto uploadStart = Clock::now();
    UploadedTextureResult prefilteredUpload =
        UploadTextureAsset(*m_rhiDevice, uploadCommandList, *prefilteredAsset, stagingBuffers);
    UploadedTextureResult irradianceUpload =
        UploadTextureAsset(*m_rhiDevice, uploadCommandList, *irradianceAsset, stagingBuffers);
    UploadedTextureResult brdfLutUpload =
        UploadTextureAsset(*m_rhiDevice, uploadCommandList, *brdfLutAsset, stagingBuffers);
    const double uploadMs = std::chrono::duration<double, std::milli>(Clock::now() - uploadStart).count();

    const uint64_t totalStagingBytes =
        prefilteredUpload.stagingBytes + irradianceUpload.stagingBytes + brdfLutUpload.stagingBytes;

    m_iblPrefilteredTexture = std::move(prefilteredUpload.texture);
    m_iblIrradianceTexture = std::move(irradianceUpload.texture);
    m_iblBrdfLutTexture = std::move(brdfLutUpload.texture);

    Logger::Log(LogLevel::Info, LogCategory::Scene,
                std::format("IBL texture stats: prefiltered={} ({} mips), irradiance={} ({} mips), brdfLut={}, "
                            "brdfLutMips={}, load {:.2f} ms, upload {:.2f} ms, staging {:.2f} MiB.",
                            prefilteredPath.filename().string(), prefilteredAsset->mipLevels,
                            irradiancePath.filename().string(), irradianceAsset->mipLevels,
                            brdfLutPath.filename().string(), brdfLutAsset->mipLevels, loadMs, uploadMs,
                            static_cast<double>(totalStagingBytes) / (1024.0 * 1024.0)));
}

void Win32Application::InitializeFreeLookCamera()
{
    WEST_ASSERT(m_sceneAsset != nullptr);
    WEST_ASSERT(m_sceneCamera != nullptr);

    const auto& boundsMin = m_sceneAsset->GetBoundsMin();
    const auto& boundsMax = m_sceneAsset->GetBoundsMax();
    const float sceneRadius = m_freeLookCamera.ResetToBounds(boundsMin, boundsMax);
    m_runtimeSettings.cameraFarPlane = std::max(500.0f, sceneRadius * 6.0f);
    m_hasLastMousePosition = false;
}

void Win32Application::UpdateFreeLookCamera(float deltaSeconds, bool blockMouseLook)
{
    WEST_ASSERT(m_sceneCamera != nullptr);
    WEST_ASSERT(m_window != nullptr);

    const HWND hwnd = static_cast<HWND>(m_window->GetNativeHandle());
    if (hwnd == nullptr)
    {
        return;
    }

    const bool rotateCamera = !blockMouseLook && (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (rotateCamera)
    {
        POINT cursorPosition{};
        if (GetCursorPos(&cursorPosition) != 0 && ScreenToClient(hwnd, &cursorPosition) != 0)
        {
            if (m_hasLastMousePosition)
            {
                const float deltaX = static_cast<float>(cursorPosition.x - m_lastMouseX);
                const float deltaY = static_cast<float>(cursorPosition.y - m_lastMouseY);
                m_freeLookCamera.Rotate(deltaX * m_runtimeSettings.cameraMouseSensitivity,
                                        -deltaY * m_runtimeSettings.cameraMouseSensitivity);
            }

            m_lastMouseX = cursorPosition.x;
            m_lastMouseY = cursorPosition.y;
            m_hasLastMousePosition = true;
        }
    }
    else
    {
        m_hasLastMousePosition = false;
    }

    const float speedMultiplier = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0 ? 4.0f : 1.0f;
    const float moveSpeed = m_runtimeSettings.cameraMoveSpeed * speedMultiplier * deltaSeconds;
    float forwardUnits = 0.0f;
    float rightUnits = 0.0f;
    float verticalUnits = 0.0f;

    if ((GetAsyncKeyState('W') & 0x8000) != 0)
    {
        forwardUnits += moveSpeed;
    }
    if ((GetAsyncKeyState('S') & 0x8000) != 0)
    {
        forwardUnits -= moveSpeed;
    }
    if ((GetAsyncKeyState('D') & 0x8000) != 0)
    {
        rightUnits += moveSpeed;
    }
    if ((GetAsyncKeyState('A') & 0x8000) != 0)
    {
        rightUnits -= moveSpeed;
    }
    if ((GetAsyncKeyState('E') & 0x8000) != 0)
    {
        verticalUnits += moveSpeed;
    }
    if ((GetAsyncKeyState('Q') & 0x8000) != 0)
    {
        verticalUnits -= moveSpeed;
    }

    m_freeLookCamera.MoveFlat(forwardUnits, rightUnits, verticalUnits);
    m_freeLookCamera.ApplyToCamera(*m_sceneCamera);
}

bool Win32Application::ConsumeKeyPress(int virtualKey)
{
    WEST_ASSERT(virtualKey >= 0 && virtualKey < static_cast<int>(m_runtimeSettings.keyState.size()));

    const bool isDown = (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
    const bool pressed = isDown && !m_runtimeSettings.keyState[virtualKey];
    m_runtimeSettings.keyState[virtualKey] = isDown;
    return pressed;
}

void Win32Application::ApplyPostPreset(uint32 presetIndex, bool logChange)
{
    m_runtimeSettings.postPresetIndex = std::min<uint32>(presetIndex, editor::GetPostPresetCount() - 1u);
    m_runtimeSettings.postPresetDirty = false;

    const editor::PostPresetDefinition& preset = editor::GetPostPreset(m_runtimeSettings.postPresetIndex);
    m_runtimeSettings.post = preset.toneMapping;
    m_runtimeSettings.bokeh = preset.bokeh;
    m_runtimeSettings.bokehEnabled = preset.enableBokeh && m_runtimeSettings.bokeh.intensity > 0.0f;
    UpdateRuntimePostWindowTitle();

    if (logChange)
    {
        LogRuntimePostState("Post preset applied");
    }
}

void Win32Application::LogRuntimePostControlsHelp() const
{
    Logger::Log(LogLevel::Info, LogCategory::Core,
                "Runtime post controls: F5 help, F6 cycle preset, F7 reset preset, F8 toggle Bokeh DOF, "
                "F9 cycle tone map, F10 cycle debug view, F11 cycle debug channel, "
                "1/2 exposure -/+, 3/4 contrast -/+, 5/6 saturation -/+, 7/8 vignette -/+, "
                "9/0 Bokeh intensity -/+, J/K brightness -/+, V/B vibrance -/+, "
                "O/P chromatic aberration -/+, N/M film grain -/+.");
}

void Win32Application::LogRuntimePostState(const char* reason) const
{
    Logger::Log(LogLevel::Info, LogCategory::Core,
                std::format("{}: preset='{}', toneMap={}, debug={} {}, exposure={:.2f}, contrast={:.2f}, "
                            "brightness={:.2f}, saturation={:.2f}, vibrance={:.2f}, vignette={:.2f}, grain={:.3f}, "
                            "chromAb={:.2f}, bokeh={} ({:.2f}), textures={}, shadows={}, ssao={}, ibl={}, "
                            "alphaDiscard={}, "
                            "light={:.2f}, env={:.2f}, diffuse={:.2f}, specular={:.2f}, fov={:.1f}.",
                            reason,
                            editor::BuildRuntimePostPresetLabel(m_runtimeSettings.postPresetIndex,
                                                                m_runtimeSettings.postPresetDirty),
                            editor::ToneMappingOperatorName(m_runtimeSettings.post.toneMappingOperator),
                            editor::PostDebugViewName(m_runtimeSettings.post.debugView),
                            editor::PostDebugChannelName(m_runtimeSettings.post.debugChannel),
                            m_runtimeSettings.post.exposure, m_runtimeSettings.post.contrast,
                            m_runtimeSettings.post.brightness, m_runtimeSettings.post.saturation,
                            m_runtimeSettings.post.vibrance, m_runtimeSettings.post.vignetteStrength,
                            m_runtimeSettings.post.filmGrainStrength, m_runtimeSettings.post.chromaticAberration,
                            OnOff(m_runtimeSettings.bokehEnabled), m_runtimeSettings.bokeh.intensity,
                            OnOff(m_runtimeSettings.texturesEnabled), OnOff(m_runtimeSettings.shadowsEnabled),
                            OnOff(m_runtimeSettings.ssaoEnabled), OnOff(m_runtimeSettings.iblEnabled),
                            OnOff(m_runtimeSettings.alphaDiscardEnabled), m_runtimeSettings.lightIntensity,
                            m_runtimeSettings.environmentIntensity, m_runtimeSettings.diffuseWeight,
                            m_runtimeSettings.specularWeight, m_runtimeSettings.cameraFovDegrees));
}

void Win32Application::UpdateRuntimePostWindowTitle() const
{
    if (!m_window)
    {
        return;
    }

    const std::string title = std::format(
        "{} | Post {} | TM {} | Dbg {} | Exp {:.2f} Sat {:.2f} | Bokeh {} {:.2f} | Tex {} Sh {} AO {} IBL "
        "{} Cut {} | GUI {} F1 | F5 Help",
        m_baseWindowTitle,
        editor::BuildRuntimePostPresetLabel(m_runtimeSettings.postPresetIndex, m_runtimeSettings.postPresetDirty),
        editor::ToneMappingOperatorName(m_runtimeSettings.post.toneMappingOperator),
        editor::PostDebugViewName(m_runtimeSettings.post.debugView), m_runtimeSettings.post.exposure,
        m_runtimeSettings.post.saturation, OnOff(m_runtimeSettings.bokehEnabled), m_runtimeSettings.bokeh.intensity,
        OnOff(m_runtimeSettings.texturesEnabled), OnOff(m_runtimeSettings.shadowsEnabled),
        OnOff(m_runtimeSettings.ssaoEnabled), OnOff(m_runtimeSettings.iblEnabled),
        OnOff(m_runtimeSettings.alphaDiscardEnabled), OnOff(m_runtimeSettings.imguiVisible));
    m_window->SetTitle(title);
}

void Win32Application::UpdateRuntimePostControls(bool blockKeyboardShortcuts)
{
    if (blockKeyboardShortcuts)
    {
        return;
    }

    bool changed = false;

    if (ConsumeKeyPress(VK_F5))
    {
        LogRuntimePostControlsHelp();
        LogRuntimePostState("Runtime post state");
    }

    if (ConsumeKeyPress(VK_F6))
    {
        const uint32 nextPreset = (m_runtimeSettings.postPresetIndex + 1u) % editor::GetPostPresetCount();
        ApplyPostPreset(nextPreset, true);
        changed = false;
    }

    if (ConsumeKeyPress(VK_F7))
    {
        ApplyPostPreset(m_runtimeSettings.postPresetIndex, true);
        changed = false;
    }

    if (ConsumeKeyPress(VK_F8))
    {
        m_runtimeSettings.bokehEnabled = !m_runtimeSettings.bokehEnabled;
        changed = true;
    }
    if (ConsumeKeyPress(VK_F9))
    {
        const uint32 nextToneMapping = (static_cast<uint32>(m_runtimeSettings.post.toneMappingOperator) + 1u) %
                                       (static_cast<uint32>(render::ToneMappingPass::ToneMappingOperator::Hable) + 1u);
        m_runtimeSettings.post.toneMappingOperator =
            static_cast<render::ToneMappingPass::ToneMappingOperator>(nextToneMapping);
        changed = true;
    }
    if (ConsumeKeyPress(VK_F10))
    {
        const uint32 nextDebugView = (static_cast<uint32>(m_runtimeSettings.post.debugView) + 1u) %
                                     (static_cast<uint32>(render::ToneMappingPass::DebugView::PostSplit) + 1u);
        m_runtimeSettings.post.debugView = static_cast<render::ToneMappingPass::DebugView>(nextDebugView);
        changed = true;
    }
    if (ConsumeKeyPress(VK_F11))
    {
        const uint32 nextDebugChannel = (static_cast<uint32>(m_runtimeSettings.post.debugChannel) + 1u) %
                                        (static_cast<uint32>(render::ToneMappingPass::DebugChannel::Luminance) + 1u);
        m_runtimeSettings.post.debugChannel = static_cast<render::ToneMappingPass::DebugChannel>(nextDebugChannel);
        changed = true;
    }

    if (ConsumeKeyPress('1'))
    {
        m_runtimeSettings.post.exposure = std::clamp(m_runtimeSettings.post.exposure - 0.10f, 0.25f, 4.0f);
        changed = true;
    }
    if (ConsumeKeyPress('2'))
    {
        m_runtimeSettings.post.exposure = std::clamp(m_runtimeSettings.post.exposure + 0.10f, 0.25f, 4.0f);
        changed = true;
    }
    if (ConsumeKeyPress('3'))
    {
        m_runtimeSettings.post.contrast = std::clamp(m_runtimeSettings.post.contrast - 0.05f, 0.50f, 2.0f);
        changed = true;
    }
    if (ConsumeKeyPress('4'))
    {
        m_runtimeSettings.post.contrast = std::clamp(m_runtimeSettings.post.contrast + 0.05f, 0.50f, 2.0f);
        changed = true;
    }
    if (ConsumeKeyPress('5'))
    {
        m_runtimeSettings.post.saturation = std::clamp(m_runtimeSettings.post.saturation - 0.05f, 0.0f, 2.0f);
        changed = true;
    }
    if (ConsumeKeyPress('6'))
    {
        m_runtimeSettings.post.saturation = std::clamp(m_runtimeSettings.post.saturation + 0.05f, 0.0f, 2.0f);
        changed = true;
    }
    if (ConsumeKeyPress('7'))
    {
        m_runtimeSettings.post.vignetteStrength =
            std::clamp(m_runtimeSettings.post.vignetteStrength - 0.02f, 0.0f, 0.50f);
        changed = true;
    }
    if (ConsumeKeyPress('8'))
    {
        m_runtimeSettings.post.vignetteStrength =
            std::clamp(m_runtimeSettings.post.vignetteStrength + 0.02f, 0.0f, 0.50f);
        changed = true;
    }
    if (ConsumeKeyPress('9'))
    {
        m_runtimeSettings.bokeh.intensity = std::clamp(m_runtimeSettings.bokeh.intensity - 0.05f, 0.0f, 1.0f);
        if (m_runtimeSettings.bokeh.intensity <= 0.0f)
        {
            m_runtimeSettings.bokehEnabled = false;
        }
        changed = true;
    }
    if (ConsumeKeyPress('0'))
    {
        m_runtimeSettings.bokeh.intensity = std::clamp(m_runtimeSettings.bokeh.intensity + 0.05f, 0.0f, 1.0f);
        if (m_runtimeSettings.bokeh.intensity > 0.0f)
        {
            m_runtimeSettings.bokehEnabled = true;
        }
        changed = true;
    }
    if (ConsumeKeyPress('O'))
    {
        m_runtimeSettings.post.chromaticAberration =
            std::clamp(m_runtimeSettings.post.chromaticAberration - 0.02f, 0.0f, 0.40f);
        changed = true;
    }
    if (ConsumeKeyPress('P'))
    {
        m_runtimeSettings.post.chromaticAberration =
            std::clamp(m_runtimeSettings.post.chromaticAberration + 0.02f, 0.0f, 0.40f);
        changed = true;
    }
    if (ConsumeKeyPress('N'))
    {
        m_runtimeSettings.post.filmGrainStrength =
            std::clamp(m_runtimeSettings.post.filmGrainStrength - 0.005f, 0.0f, 0.08f);
        changed = true;
    }
    if (ConsumeKeyPress('M'))
    {
        m_runtimeSettings.post.filmGrainStrength =
            std::clamp(m_runtimeSettings.post.filmGrainStrength + 0.005f, 0.0f, 0.08f);
        changed = true;
    }
    if (ConsumeKeyPress('J'))
    {
        m_runtimeSettings.post.brightness = std::clamp(m_runtimeSettings.post.brightness - 0.02f, -0.5f, 0.5f);
        changed = true;
    }
    if (ConsumeKeyPress('K'))
    {
        m_runtimeSettings.post.brightness = std::clamp(m_runtimeSettings.post.brightness + 0.02f, -0.5f, 0.5f);
        changed = true;
    }
    if (ConsumeKeyPress('V'))
    {
        m_runtimeSettings.post.vibrance = std::clamp(m_runtimeSettings.post.vibrance - 0.05f, -1.0f, 1.0f);
        changed = true;
    }
    if (ConsumeKeyPress('B'))
    {
        m_runtimeSettings.post.vibrance = std::clamp(m_runtimeSettings.post.vibrance + 0.05f, -1.0f, 1.0f);
        changed = true;
    }

    if (changed)
    {
        m_runtimeSettings.postPresetDirty = true;
        UpdateRuntimePostWindowTitle();
        LogRuntimePostState("Runtime post updated");
    }
}

void Win32Application::BuildTelemetryOverlay()
{
    WEST_ASSERT(m_frameTelemetry != nullptr);

    const editor::TelemetryOverlayDesc overlayDesc{
        .frameTelemetry = m_frameTelemetry.get(),
        .deviceCaps = m_rhiDevice != nullptr ? m_rhiDevice->GetCapabilities() : rhi::RHIDeviceCaps{},
        .backendName = BackendName(m_backend),
        .visibleDrawCount = m_lastGPUDrivenVisibleCount,
        .candidateDrawCount = m_sceneDrawCount,
    };
    editor::BuildTelemetryOverlay(overlayDesc);
}

void Win32Application::BuildImGuiControlPanel()
{
    WEST_ASSERT(m_imguiRenderer != nullptr);
    WEST_ASSERT(m_frameTelemetry != nullptr);

    if (!m_runtimeSettings.imguiVisible)
    {
        return;
    }

    BuildTelemetryOverlay();

    bool titleChanged = false;
    bool postChanged = false;
    bool renderChanged = false;
    bool inspectorChanged = false;
    bool windowOpen = true;
    const std::string currentPreset =
        editor::BuildRuntimePostPresetLabel(m_runtimeSettings.postPresetIndex, m_runtimeSettings.postPresetDirty);
    const std::span<const editor::PostPresetDefinition> postPresets = editor::GetPostPresets();

    ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(460.0f, 860.0f), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("WestEngine Runtime", &windowOpen, ImGuiWindowFlags_NoCollapse))
    {
        ImGui::Text("Backend: %s", BackendName(m_backend));
        const std::string sceneName = SceneDisplayName(m_sceneDesc);
        ImGui::Text("Scene: %s", sceneName.c_str());
        ImGui::Text("GPU-driven: %s", OnOff(m_gpuDrivenAvailable));
        const float cullPercent = m_sceneDrawCount > 0 ? 100.0f - (static_cast<float>(m_lastGPUDrivenVisibleCount) /
                                                                   static_cast<float>(m_sceneDrawCount) * 100.0f)
                                                       : 0.0f;
        ImGui::Text("Visible Draws: %u / %u (%.1f%% culled)", m_lastGPUDrivenVisibleCount, m_sceneDrawCount,
                    cullPercent);
        ImGui::Separator();

        if (ImGui::Checkbox("Capture GUI Input", &m_runtimeSettings.imguiCaptureInput))
        {
            titleChanged = true;
        }
        ImGui::Separator();

        renderChanged |= editor::BuildRenderFeatureControls(m_runtimeSettings);
        ImGui::Separator();

        inspectorChanged |= editor::BuildLightingAndPBRControls(m_runtimeSettings);

        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen))
        {
            WEST_ASSERT(m_sceneAsset != nullptr);

            const auto& boundsMin = m_sceneAsset->GetBoundsMin();
            const auto& boundsMax = m_sceneAsset->GetBoundsMax();
            const std::array<float, 3> sceneCenter = {
                (boundsMin[0] + boundsMax[0]) * 0.5f,
                (boundsMin[1] + boundsMax[1]) * 0.5f,
                (boundsMin[2] + boundsMax[2]) * 0.5f,
            };
            const std::array<float, 3> extents = {
                boundsMax[0] - boundsMin[0],
                boundsMax[1] - boundsMin[1],
                boundsMax[2] - boundsMin[2],
            };
            const float sceneRadius = std::max(1.0f, scene::Length3(extents) * 0.5f);
            const float positionLimit = std::max(25.0f, sceneRadius * 2.5f);
            const float farPlaneLimit = std::max(2000.0f, sceneRadius * 20.0f);

            std::array<float, 3> cameraPosition = m_freeLookCamera.GetPosition();
            float yawDegrees = RadiansToDegrees(m_freeLookCamera.GetYawRadians());
            float pitchDegrees = RadiansToDegrees(m_freeLookCamera.GetPitchRadians());

            if (ImGui::SliderFloat3("Position", cameraPosition.data(), -positionLimit, positionLimit, "%.2f"))
            {
                m_freeLookCamera.SetPosition(cameraPosition);
                inspectorChanged = true;
            }
            if (ImGui::SliderFloat("Yaw", &yawDegrees, -180.0f, 180.0f, "%.1f deg"))
            {
                m_freeLookCamera.SetYawRadians(DegreesToRadians(yawDegrees));
                inspectorChanged = true;
            }
            if (ImGui::SliderFloat("Pitch", &pitchDegrees, -83.0f, 83.0f, "%.1f deg"))
            {
                m_freeLookCamera.SetPitchRadians(DegreesToRadians(pitchDegrees));
                inspectorChanged = true;
            }

            inspectorChanged |=
                ImGui::SliderFloat("Move Speed", &m_runtimeSettings.cameraMoveSpeed, 0.5f, 80.0f, "%.1f");
            inspectorChanged |= ImGui::SliderFloat("Mouse Sensitivity", &m_runtimeSettings.cameraMouseSensitivity,
                                                   0.0005f, 0.02f, "%.4f");
            inspectorChanged |=
                ImGui::SliderFloat("Field of View", &m_runtimeSettings.cameraFovDegrees, 30.0f, 120.0f, "%.1f deg");
            inspectorChanged |=
                ImGui::SliderFloat("Near Plane", &m_runtimeSettings.cameraNearPlane, 0.01f, 5.0f, "%.2f");
            inspectorChanged |=
                ImGui::SliderFloat("Far Plane", &m_runtimeSettings.cameraFarPlane, 50.0f, farPlaneLimit, "%.0f");

            if (ImGui::Button("Reset View"))
            {
                InitializeFreeLookCamera();
                inspectorChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Side View"))
            {
                m_freeLookCamera.SetPose(
                    {
                        sceneCenter[0] + (sceneRadius * 1.35f),
                        sceneCenter[1] + (sceneRadius * 0.22f),
                        sceneCenter[2],
                    },
                    std::numbers::pi_v<float>, 0.0f);
                inspectorChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Top View"))
            {
                m_freeLookCamera.SetPose(
                    {
                        sceneCenter[0],
                        sceneCenter[1] + (sceneRadius * 1.65f),
                        sceneCenter[2] + 0.001f,
                    },
                    -std::numbers::pi_v<float> * 0.5f, scene::FreeLookCameraController::kMinPitchRadians);
                inspectorChanged = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Culling Proof View"))
            {
                m_freeLookCamera.SetPose(
                    {
                        sceneCenter[0] + (sceneRadius * 0.85f),
                        sceneCenter[1] + (sceneRadius * 0.16f),
                        sceneCenter[2] + (sceneRadius * 1.05f),
                    },
                    -std::numbers::pi_v<float> * 0.82f, -0.06f);
                m_runtimeSettings.cameraFovDegrees = 38.0f;
                inspectorChanged = true;
            }
        }

        ImGui::Separator();

        if (ImGui::BeginCombo("Preset", currentPreset.c_str()))
        {
            for (uint32 index = 0; index < static_cast<uint32>(postPresets.size()); ++index)
            {
                const bool selected = index == m_runtimeSettings.postPresetIndex;
                if (ImGui::Selectable(postPresets[index].name, selected))
                {
                    ApplyPostPreset(index, false);
                    titleChanged = true;
                }
                if (selected)
                {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        if (ImGui::Button("Reset Preset"))
        {
            ApplyPostPreset(m_runtimeSettings.postPresetIndex, false);
            titleChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Log State"))
        {
            LogRuntimePostState("Runtime GUI state");
        }

        ImGui::Separator();
        postChanged |= editor::BuildPostProcessingControls(m_runtimeSettings);

        ImGui::Separator();
        editor::BuildRuntimeHotkeyHelp();
    }
    ImGui::End();

    if (!windowOpen)
    {
        m_runtimeSettings.imguiVisible = false;
        titleChanged = true;
    }

    if (postChanged)
    {
        m_runtimeSettings.postPresetDirty = true;
        UpdateRuntimePostWindowTitle();
    }
    else if (titleChanged || renderChanged || inspectorChanged)
    {
        UpdateRuntimePostWindowTitle();
    }

    if (renderChanged)
    {
        LogRuntimePostState("Runtime render features updated");
    }
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

    auto recordList = [](rhi::IRHICommandList* commandList, uint32 seed)
    {
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

    auto now = []() { return std::chrono::high_resolution_clock::now(); };

    const auto singleStart = now();
    for (uint32 i = 0; i < workerCount; ++i)
    {
        recordList(benchmarkLists[i].get(), i);
    }
    const auto singleEnd = now();

    TaskSystem taskSystem;
    taskSystem.Initialize(workerCount);

    const auto multiStart = now();
    taskSystem.Dispatch(workerCount, [&](uint32 taskIndex) { recordList(benchmarkLists[taskIndex].get(), taskIndex); });
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

    if (ConsumeKeyPress(VK_F1))
    {
        m_runtimeSettings.imguiVisible = !m_runtimeSettings.imguiVisible;
        UpdateRuntimePostWindowTitle();
        Logger::Log(LogLevel::Info, LogCategory::Core,
                    std::format("Runtime ImGui overlay {}.", m_runtimeSettings.imguiVisible ? "enabled" : "disabled"));
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

    if (m_gpuDrivenAvailable && frameIndex < m_gpuDrivenReadbackPending.size() &&
        m_gpuDrivenReadbackPending[frameIndex] && frameIndex < m_gpuDrivenCountReadbackBuffers.size())
    {
        auto* mappedVisibleCount = static_cast<const uint32_t*>(m_gpuDrivenCountReadbackBuffers[frameIndex]->Map());
        WEST_CHECK(mappedVisibleCount != nullptr, "Failed to map GPU-driven visible-count readback buffer");
        m_lastGPUDrivenVisibleCount = mappedVisibleCount[0];
        m_gpuDrivenCountReadbackBuffers[frameIndex]->Unmap();
        m_gpuDrivenReadbackPending[frameIndex] = false;

        if (!m_gpuDrivenVisibilityLogged || m_maxFrameCount > 0 ||
            m_lastLoggedVisibleCount != m_lastGPUDrivenVisibleCount)
        {
            Logger::Log(LogLevel::Info, LogCategory::Scene,
                        std::format("GPU-driven visibility: visibleDraws={} / candidateDraws={} ({:.1f}% culled), "
                                    "GBuffer submissions {} -> {}.",
                                    m_lastGPUDrivenVisibleCount, m_sceneDrawCount,
                                    m_sceneDrawCount > 0 ? (100.0 - (static_cast<double>(m_lastGPUDrivenVisibleCount) /
                                                                     static_cast<double>(m_sceneDrawCount) * 100.0))
                                                         : 0.0,
                                    m_sceneDrawCount, m_sceneDrawCount > 0 ? 1u : 0u));
            m_gpuDrivenVisibilityLogged = true;
            m_lastLoggedVisibleCount = m_lastGPUDrivenVisibleCount;
        }
    }

    // Until RenderGraph reserves this frame's actual submit fence, deletions before recording only need the
    // already-completed slot fence.
    m_rhiDevice->SetCurrentFrameFenceValue(m_fenceValues[frameIndex]);

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

    // 3. Update scene camera + frame constants, then build/execute the frame Render Graph
    auto* backBuffer = m_swapChain->GetCurrentBackBuffer();
    WEST_ASSERT(m_shadowMapPass != nullptr);
    WEST_ASSERT(m_gBufferPass != nullptr);
    WEST_ASSERT(m_ssaoPass != nullptr);
    WEST_ASSERT(m_deferredLightingPass != nullptr);
    WEST_ASSERT(m_bokehDOFPass != nullptr);
    WEST_ASSERT(m_toneMappingPass != nullptr);
    WEST_ASSERT(m_imguiRenderer != nullptr);
    WEST_ASSERT(m_imguiPass != nullptr);
    WEST_ASSERT(m_transientResourcePool != nullptr);
    WEST_ASSERT(m_sceneCamera != nullptr);
    WEST_ASSERT(m_sceneAsset != nullptr);
    WEST_ASSERT(frameIndex < m_frameConstantsBuffers.size());
    rhi::IRHIBuffer* frameConstantsBuffer = m_frameConstantsBuffers[frameIndex].get();
    WEST_ASSERT(frameConstantsBuffer != nullptr);
    WEST_ASSERT(m_materialBuffer != nullptr);

    const float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    const auto& boundsMin = m_sceneAsset->GetBoundsMin();
    const auto& boundsMax = m_sceneAsset->GetBoundsMax();
    const std::array<float, 3> extents = {
        boundsMax[0] - boundsMin[0],
        boundsMax[1] - boundsMin[1],
        boundsMax[2] - boundsMin[2],
    };
    const std::array<float, 3> sceneCenter = {
        (boundsMin[0] + boundsMax[0]) * 0.5f,
        (boundsMin[1] + boundsMax[1]) * 0.5f,
        (boundsMin[2] + boundsMax[2]) * 0.5f,
    };
    const float sceneRadius = std::max(1.0f, scene::Length3(extents) * 0.5f);
    const float deltaSeconds = static_cast<float>(m_timer.GetDeltaTime());
    if (m_frameTelemetry != nullptr)
    {
        m_frameTelemetry->RecordFrameDelta(deltaSeconds);
        if (m_frameGraph != nullptr && m_frameGraphWidth != 0 && m_frameGraphHeight != 0)
        {
            m_frameTelemetry->CaptureRenderGraph(m_frameGraph->GetCompiledGraph());
        }
    }

    if (m_gpuTimerManager != nullptr && m_frameTelemetry != nullptr)
    {
        m_gpuTimerManager->ConsumeCompletedFrame(frameIndex, *m_frameTelemetry);
    }

    const HWND hwnd = static_cast<HWND>(m_window->GetNativeHandle());
    const editor::ImGuiRenderer::InputState imguiInput =
        BuildImGuiInputState(hwnd, windowWidth, windowHeight, deltaSeconds);
    m_imguiRenderer->BeginFrame(imguiInput);
    m_runtimeSettings.imguiWantsMouseCapture =
        m_runtimeSettings.imguiCaptureInput && m_imguiRenderer->WantsMouseCapture();
    m_runtimeSettings.imguiWantsKeyboardCapture =
        m_runtimeSettings.imguiCaptureInput && m_imguiRenderer->WantsKeyboardCapture();
    UpdateFreeLookCamera(deltaSeconds, m_runtimeSettings.imguiWantsMouseCapture);
    UpdateRuntimePostControls(m_runtimeSettings.imguiWantsKeyboardCapture);
    BuildImGuiControlPanel();
    m_imguiRenderer->EndFrame(frameIndex);
    m_runtimeSettings.imguiWantsMouseCapture =
        m_runtimeSettings.imguiCaptureInput && m_imguiRenderer->WantsMouseCapture();
    m_runtimeSettings.imguiWantsKeyboardCapture =
        m_runtimeSettings.imguiCaptureInput && m_imguiRenderer->WantsKeyboardCapture();

    m_runtimeSettings.cameraFovDegrees = std::clamp(m_runtimeSettings.cameraFovDegrees, 30.0f, 120.0f);
    m_runtimeSettings.cameraNearPlane = std::clamp(m_runtimeSettings.cameraNearPlane, 0.01f, 5.0f);
    m_runtimeSettings.cameraFarPlane =
        std::max(m_runtimeSettings.cameraNearPlane + 1.0f, m_runtimeSettings.cameraFarPlane);
    m_runtimeSettings.ssaoRadius = std::clamp(m_runtimeSettings.ssaoRadius, 0.01f, 0.50f);
    m_runtimeSettings.ssaoBias = std::clamp(m_runtimeSettings.ssaoBias, 0.0f, 0.10f);
    m_runtimeSettings.ssaoSampleCount = std::clamp(m_runtimeSettings.ssaoSampleCount, 0, 64);
    m_runtimeSettings.ssaoPower = std::clamp(m_runtimeSettings.ssaoPower, 0.5f, 4.0f);

    const float fovYRadians = DegreesToRadians(m_runtimeSettings.cameraFovDegrees);
    const float tanHalfFov = std::tan(fovYRadians * 0.5f);
    const float nearPlane = m_runtimeSettings.cameraNearPlane;
    const float farPlane = m_runtimeSettings.cameraFarPlane;
    m_sceneCamera->SetPerspective(fovYRadians, aspectRatio, nearPlane, farPlane);
    m_freeLookCamera.ApplyToCamera(*m_sceneCamera);

    auto* frameConstants = static_cast<GPUFrameConstants*>(frameConstantsBuffer->Map());
    WEST_CHECK(frameConstants != nullptr, "Failed to map frame constants buffer");
    std::memcpy(frameConstants->viewProjection, m_sceneCamera->GetViewProjectionMatrix().data(),
                sizeof(frameConstants->viewProjection));
    const std::array<float, 3>& eye = m_sceneCamera->GetPosition();
    const std::array<float, 3> cameraForward = m_freeLookCamera.GetForward();
    const std::array<float, 3> cameraRight = m_freeLookCamera.GetRight();
    const std::array<float, 3> cameraUp = m_freeLookCamera.GetUp();
    frameConstants->cameraPosition[0] = eye[0];
    frameConstants->cameraPosition[1] = eye[1];
    frameConstants->cameraPosition[2] = eye[2];
    frameConstants->cameraPosition[3] = 1.0f;
    frameConstants->cameraForward[0] = cameraForward[0];
    frameConstants->cameraForward[1] = cameraForward[1];
    frameConstants->cameraForward[2] = cameraForward[2];
    frameConstants->cameraForward[3] = 0.0f;
    frameConstants->cameraRight[0] = cameraRight[0];
    frameConstants->cameraRight[1] = cameraRight[1];
    frameConstants->cameraRight[2] = cameraRight[2];
    frameConstants->cameraRight[3] = 0.0f;
    frameConstants->cameraUp[0] = cameraUp[0];
    frameConstants->cameraUp[1] = cameraUp[1];
    frameConstants->cameraUp[2] = cameraUp[2];
    frameConstants->cameraUp[3] = 0.0f;
    frameConstants->cameraProjectionParams[0] = tanHalfFov;
    frameConstants->cameraProjectionParams[1] = aspectRatio;
    frameConstants->cameraProjectionParams[2] = nearPlane;
    frameConstants->cameraProjectionParams[3] = farPlane;

    const float tanX = tanHalfFov * aspectRatio;
    frameConstants->cameraFrustumParams[0] = std::sqrt(1.0f + tanX * tanX);
    frameConstants->cameraFrustumParams[1] = std::sqrt(1.0f + tanHalfFov * tanHalfFov);
    frameConstants->cameraFrustumParams[2] = 0.0f;
    frameConstants->cameraFrustumParams[3] = 0.0f;

    const std::array<float, 3> lightDirection =
        MakeDirectionalLightVector(m_runtimeSettings.lightAzimuthDegrees, m_runtimeSettings.lightElevationDegrees);
    frameConstants->lightDirection[0] = lightDirection[0];
    frameConstants->lightDirection[1] = lightDirection[1];
    frameConstants->lightDirection[2] = lightDirection[2];
    frameConstants->lightDirection[3] = 0.0f;

    frameConstants->lightColor[0] = m_runtimeSettings.lightIntensity;
    frameConstants->lightColor[1] = m_runtimeSettings.lightIntensity * 0.9423f;
    frameConstants->lightColor[2] = m_runtimeSettings.lightIntensity * 0.8654f;
    frameConstants->lightColor[3] = 1.0f;

    frameConstants->ambientColor[0] = m_runtimeSettings.environmentIntensity;
    frameConstants->ambientColor[1] = m_runtimeSettings.environmentIntensity;
    frameConstants->ambientColor[2] = m_runtimeSettings.environmentIntensity;
    frameConstants->ambientColor[3] = 1.0f;
    frameConstants->skyZenithColor[0] = 0.09f;
    frameConstants->skyZenithColor[1] = 0.20f;
    frameConstants->skyZenithColor[2] = 0.42f;
    frameConstants->skyZenithColor[3] = 1.0f;
    frameConstants->skyHorizonColor[0] = 0.68f;
    frameConstants->skyHorizonColor[1] = 0.75f;
    frameConstants->skyHorizonColor[2] = 0.86f;
    frameConstants->skyHorizonColor[3] = 1.0f;
    frameConstants->groundColor[0] = 0.14f;
    frameConstants->groundColor[1] = 0.12f;
    frameConstants->groundColor[2] = 0.11f;
    frameConstants->groundColor[3] = 1.0f;
    frameConstants->skyParams[0] = 4.0f;
    frameConstants->skyParams[1] = 0.35f;
    frameConstants->skyParams[2] = 4096.0f;
    frameConstants->skyParams[3] = 128.0f;
    const float prefilteredMaxMip =
        m_iblPrefilteredTexture != nullptr
            ? static_cast<float>(std::max(0u, m_iblPrefilteredTexture->GetDesc().mipLevels - 1u))
            : 0.0f;
    frameConstants->iblParams[0] = 1.0f;
    frameConstants->iblParams[1] = 1.0f;
    frameConstants->iblParams[2] = 1.0f;
    frameConstants->iblParams[3] = prefilteredMaxMip;

    const std::array<float, 3> lightEye = {
        sceneCenter[0] - (lightDirection[0] * sceneRadius * 2.0f),
        sceneCenter[1] - (lightDirection[1] * sceneRadius * 2.0f),
        sceneCenter[2] - (lightDirection[2] * sceneRadius * 2.0f),
    };
    const std::array<float, 3> lightUp = std::abs(scene::Dot3(lightDirection, {0.0f, 1.0f, 0.0f})) > 0.95f
                                             ? std::array<float, 3>{0.0f, 0.0f, 1.0f}
                                             : std::array<float, 3>{0.0f, 1.0f, 0.0f};
    const std::array<float, 16> lightView = scene::CreateLookAtRH(lightEye, sceneCenter, lightUp);
    const std::array<float, 16> lightProjection =
        CreateOrthographicOffCenterRH(-sceneRadius, sceneRadius, -sceneRadius, sceneRadius, 0.5f, sceneRadius * 4.5f);
    const std::array<float, 16> lightViewProjection = scene::MultiplyMatrix4x4(lightView, lightProjection);
    std::memcpy(frameConstants->lightViewProjection, lightViewProjection.data(),
                sizeof(frameConstants->lightViewProjection));
    frameConstants->shadowParams[0] = m_runtimeSettings.shadowBias;
    frameConstants->shadowParams[1] = m_runtimeSettings.shadowNormalBias;
    frameConstants->shadowParams[2] = 1.0f / 2048.0f;
    frameConstants->shadowParams[3] = 1.0f / 2048.0f;
    frameConstants->renderFeatureFlags[0] = m_runtimeSettings.shadowsEnabled ? 1.0f : 0.0f;
    frameConstants->renderFeatureFlags[1] = m_runtimeSettings.ssaoEnabled ? 1.0f : 0.0f;
    frameConstants->renderFeatureFlags[2] = m_runtimeSettings.iblEnabled ? 1.0f : 0.0f;
    frameConstants->renderFeatureFlags[3] = m_runtimeSettings.alphaDiscardEnabled ? 1.0f : 0.0f;
    frameConstants->materialParams[0] = m_runtimeSettings.diffuseWeight;
    frameConstants->materialParams[1] = m_runtimeSettings.specularWeight;
    frameConstants->materialParams[2] = 0.0f;
    frameConstants->materialParams[3] = m_runtimeSettings.texturesEnabled ? 1.0f : 0.0f;
    frameConstants->pbrParams[0] = m_runtimeSettings.metallicScale;
    frameConstants->pbrParams[1] = m_runtimeSettings.roughnessScale;
    frameConstants->ssaoParams[0] = m_runtimeSettings.ssaoRadius;
    frameConstants->ssaoParams[1] = m_runtimeSettings.ssaoBias;
    frameConstants->ssaoParams[2] = static_cast<float>(m_runtimeSettings.ssaoSampleCount);
    frameConstants->ssaoParams[3] = m_runtimeSettings.ssaoPower;
    frameConstantsBuffer->Unmap();

    const rhi::RHIResourceState backBufferInitialState =
        m_isFirstFrame[imageIndex] ? rhi::RHIResourceState::Undefined : rhi::RHIResourceState::Present;
    EnsureFrameGraph(backBuffer, backBufferInitialState, frameIndex);
    WEST_ASSERT(m_frameGraph != nullptr);
    if (m_frameTelemetry != nullptr)
    {
        m_frameTelemetry->CaptureRenderGraph(m_frameGraph->GetCompiledGraph());
    }

    std::vector<render::StaticMeshDrawItem> drawItems;
    drawItems.reserve(m_sceneAsset->GetInstances().size());
    for (const scene::InstanceData& instance : m_sceneAsset->GetInstances())
    {
        WEST_ASSERT(instance.meshIndex < m_sceneMeshResources.size());
        const SceneMeshResource& meshResource = m_sceneMeshResources[instance.meshIndex];

        render::StaticMeshDrawItem drawItem{};
        drawItem.vertexBuffer = meshResource.vertexBuffer;
        drawItem.indexBuffer = meshResource.indexBuffer;
        if (meshResource.vertexBuffer == m_sceneVertexBuffer.get())
        {
            drawItem.vertexBufferHandle = m_frameSceneVertexBufferHandle;
        }
        if (meshResource.indexBuffer == m_sceneIndexBuffer.get())
        {
            drawItem.indexBufferHandle = m_frameSceneIndexBufferHandle;
        }
        drawItem.vertexOffsetBytes = meshResource.vertexOffsetBytes;
        drawItem.indexOffsetBytes = meshResource.indexOffsetBytes;
        drawItem.indexCount = meshResource.indexCount;
        drawItem.materialIndex = meshResource.materialIndex;
        drawItem.modelMatrix = instance.modelMatrix;
        drawItems.push_back(drawItem);
    }

    rhi::IRHISampler* materialSampler = m_materialStableSampler.get();
    WEST_ASSERT(materialSampler != nullptr);
    m_shadowMapPass->SetMaterialSampler(materialSampler);
    m_gBufferPass->SetMaterialSampler(materialSampler);
    m_shadowMapPass->SetSceneData(drawItems, m_frameConstantsBufferHandle, m_frameMaterialBufferHandle);
    m_gBufferPass->SetSceneData(drawItems, m_frameConstantsBufferHandle, m_frameMaterialBufferHandle,
                                m_frameSceneDrawBufferHandle);
    m_ssaoPass->SetFrameData(m_frameConstantsBufferHandle);
    if (m_gpuDrivenAvailable)
    {
        m_gBufferPass->SetIndirectBuffers(m_frameGPUDrivenIndirectArgsHandle, m_frameGPUDrivenIndirectCountHandle,
                                          m_frameSceneVertexBufferHandle, m_frameSceneIndexBufferHandle,
                                          m_sceneDrawCount);
    }
    else
    {
        m_gBufferPass->DisableIndirect();
    }
    m_deferredLightingPass->SetFrameData(m_frameConstantsBufferHandle);
    m_deferredLightingPass->SetIBLTextures(m_frameIBLPrefilteredHandle, m_frameIBLIrradianceHandle,
                                           m_frameIBLBrdfLutHandle);
    m_bokehDOFPass->SetFrameData(m_frameConstantsBufferHandle);
    m_toneMappingPass->SetPostSettings(m_runtimeSettings.post);
    render::BokehDOFPass::Settings effectiveBokehSettings = m_runtimeSettings.bokeh;
    if (!m_runtimeSettings.bokehEnabled)
    {
        effectiveBokehSettings.intensity = 0.0f;
    }
    m_bokehDOFPass->SetSettings(effectiveBokehSettings);

    render::RenderGraphTimestampProfilingDesc timestampProfiling{};
    if (m_gpuTimerManager != nullptr)
    {
        timestampProfiling = m_gpuTimerManager->BeginFrame(*m_rhiDevice, frameIndex, m_frameGraph->GetCompiledGraph());
    }

    render::RenderGraph::ExecuteDesc executeDesc{
        .device = *m_rhiDevice,
        .timelineFence = *m_frameFence,
        .transientResourcePool = *m_transientResourcePool,
        .commandListPool = m_commandListPool.get(),
        .waitSemaphore = acquireSem,
        .signalSemaphore = m_backend == rhi::RHIBackend::Vulkan ? m_presentSemaphores[imageIndex].get() : nullptr,
        .timestampProfiling = timestampProfiling,
    };

    m_fenceValues[frameIndex] = m_frameGraph->Execute(executeDesc);
    if (m_gpuTimerManager != nullptr)
    {
        m_gpuTimerManager->EndFrame(frameIndex, m_fenceValues[frameIndex], m_frameGraph->GetCompiledGraph());
    }
    if (m_gpuDrivenAvailable && frameIndex < m_gpuDrivenReadbackPending.size())
    {
        m_gpuDrivenReadbackPending[frameIndex] = true;
    }
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
    m_frameGraphWidth = 0;
    m_frameGraphHeight = 0;
    m_frameGraphBackBufferFormat = rhi::RHIFormat::Unknown;
    if (m_frameGraph)
    {
        m_frameGraph->Reset();
    }

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

void Win32Application::EnsureFrameGraph(rhi::IRHITexture* backBuffer, rhi::RHIResourceState initialState,
                                        uint32 frameIndex)
{
    WEST_ASSERT(backBuffer != nullptr);
    WEST_ASSERT(m_frameGraph != nullptr);
    WEST_ASSERT(m_shadowMapPass != nullptr);
    WEST_ASSERT(m_gBufferPass != nullptr);
    WEST_ASSERT(m_ssaoPass != nullptr);
    WEST_ASSERT(m_deferredLightingPass != nullptr);
    WEST_ASSERT(m_bokehDOFPass != nullptr);
    WEST_ASSERT(m_toneMappingPass != nullptr);
    WEST_ASSERT(m_imguiPass != nullptr);
    WEST_ASSERT(frameIndex < m_frameConstantsBuffers.size());
    rhi::IRHIBuffer* frameConstantsBuffer = m_frameConstantsBuffers[frameIndex].get();
    WEST_ASSERT(frameConstantsBuffer != nullptr);
    WEST_ASSERT(m_materialBuffer != nullptr);
    WEST_ASSERT(m_sceneDrawBuffer != nullptr);
    WEST_ASSERT(m_iblPrefilteredTexture != nullptr);
    WEST_ASSERT(m_iblIrradianceTexture != nullptr);
    WEST_ASSERT(m_iblBrdfLutTexture != nullptr);

    const rhi::RHITextureDesc& backBufferDesc = backBuffer->GetDesc();
    const bool needsRebuild = !m_frameBackBufferHandle.IsValid() || m_frameGraphWidth != backBufferDesc.width ||
                              m_frameGraphHeight != backBufferDesc.height ||
                              m_frameGraphBackBufferFormat != backBufferDesc.format;
    const bool hasPreviousGPUDrivenState = frameIndex < m_fenceValues.size() && m_fenceValues[frameIndex] > 0;
    rhi::IRHIBuffer* indirectArgsBuffer = m_gpuDrivenAvailable && frameIndex < m_gpuDrivenIndirectArgsBuffers.size()
                                              ? m_gpuDrivenIndirectArgsBuffers[frameIndex].get()
                                              : nullptr;
    rhi::IRHIBuffer* indirectCountBuffer = m_gpuDrivenAvailable && frameIndex < m_gpuDrivenIndirectCountBuffers.size()
                                               ? m_gpuDrivenIndirectCountBuffers[frameIndex].get()
                                               : nullptr;
    rhi::IRHIBuffer* readbackBuffer = m_gpuDrivenAvailable && frameIndex < m_gpuDrivenCountReadbackBuffers.size()
                                          ? m_gpuDrivenCountReadbackBuffers[frameIndex].get()
                                          : nullptr;

    if (needsRebuild)
    {
        m_frameGraph->Reset();
        m_frameBackBufferHandle = m_frameGraph->ImportTexture(backBuffer, initialState, rhi::RHIResourceState::Present,
                                                              "SwapchainBackBuffer");
        m_frameConstantsBufferHandle =
            m_frameGraph->ImportBuffer(frameConstantsBuffer, rhi::RHIResourceState::ShaderResource,
                                       rhi::RHIResourceState::ShaderResource, "FrameBuffer");
        m_frameMaterialBufferHandle =
            m_frameGraph->ImportBuffer(m_materialBuffer.get(), rhi::RHIResourceState::ShaderResource,
                                       rhi::RHIResourceState::ShaderResource, "MaterialBuffer");
        m_frameSceneDrawBufferHandle =
            m_frameGraph->ImportBuffer(m_sceneDrawBuffer.get(), rhi::RHIResourceState::ShaderResource,
                                       rhi::RHIResourceState::ShaderResource, "SceneDrawBuffer");
        m_frameSceneVertexBufferHandle = {};
        m_frameSceneIndexBufferHandle = {};
        if (m_sceneVertexBuffer != nullptr)
        {
            m_frameSceneVertexBufferHandle =
                m_frameGraph->ImportBuffer(m_sceneVertexBuffer.get(), rhi::RHIResourceState::VertexBuffer,
                                           rhi::RHIResourceState::VertexBuffer, "SceneVertexBuffer");
        }
        if (m_sceneIndexBuffer != nullptr)
        {
            m_frameSceneIndexBufferHandle =
                m_frameGraph->ImportBuffer(m_sceneIndexBuffer.get(), rhi::RHIResourceState::IndexBuffer,
                                           rhi::RHIResourceState::IndexBuffer, "SceneIndexBuffer");
        }
        m_frameIBLPrefilteredHandle =
            m_frameGraph->ImportTexture(m_iblPrefilteredTexture.get(), rhi::RHIResourceState::ShaderResource,
                                        rhi::RHIResourceState::ShaderResource, "IBLPrefilteredEnvironment");
        m_frameIBLIrradianceHandle =
            m_frameGraph->ImportTexture(m_iblIrradianceTexture.get(), rhi::RHIResourceState::ShaderResource,
                                        rhi::RHIResourceState::ShaderResource, "IBLIrradianceEnvironment");
        m_frameIBLBrdfLutHandle =
            m_frameGraph->ImportTexture(m_iblBrdfLutTexture.get(), rhi::RHIResourceState::ShaderResource,
                                        rhi::RHIResourceState::ShaderResource, "IBLBrdfLut");

        if (m_gpuDrivenAvailable)
        {
            WEST_ASSERT(m_gpuDrivenCountResetBuffer != nullptr);
            WEST_ASSERT(indirectArgsBuffer != nullptr);
            WEST_ASSERT(indirectCountBuffer != nullptr);
            WEST_ASSERT(readbackBuffer != nullptr);

            m_frameGPUDrivenCountResetBufferHandle =
                m_frameGraph->ImportBuffer(m_gpuDrivenCountResetBuffer.get(), rhi::RHIResourceState::CopySource,
                                           rhi::RHIResourceState::CopySource, "GPUDrivenIndirectCountReset");
            m_frameGPUDrivenIndirectArgsHandle = m_frameGraph->ImportBuffer(
                indirectArgsBuffer,
                hasPreviousGPUDrivenState ? rhi::RHIResourceState::IndirectArgument : rhi::RHIResourceState::Undefined,
                rhi::RHIResourceState::IndirectArgument, "GPUDrivenIndirectArgs");
            m_frameGPUDrivenIndirectCountHandle = m_frameGraph->ImportBuffer(
                indirectCountBuffer,
                hasPreviousGPUDrivenState ? rhi::RHIResourceState::CopySource : rhi::RHIResourceState::Undefined,
                rhi::RHIResourceState::CopySource, "GPUDrivenIndirectCount");
            m_frameGPUDrivenReadbackHandle =
                m_frameGraph->ImportBuffer(readbackBuffer, rhi::RHIResourceState::CopyDest,
                                           rhi::RHIResourceState::CopyDest, "GPUDrivenVisibleCountReadback");
        }

        rhi::RHITextureDesc sceneColorDesc{};
        sceneColorDesc.width = backBufferDesc.width;
        sceneColorDesc.height = backBufferDesc.height;
        sceneColorDesc.format = rhi::RHIFormat::RGBA16_FLOAT;
        sceneColorDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::ShaderResource;
        sceneColorDesc.debugName = "SceneColorHDR";

        rhi::RHITextureDesc gbufferDesc{};
        gbufferDesc.width = backBufferDesc.width;
        gbufferDesc.height = backBufferDesc.height;
        gbufferDesc.format = rhi::RHIFormat::RGBA16_FLOAT;
        gbufferDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::ShaderResource;
        gbufferDesc.debugName = "GBufferTransient";

        rhi::RHITextureDesc ambientOcclusionDesc{};
        ambientOcclusionDesc.width = backBufferDesc.width;
        ambientOcclusionDesc.height = backBufferDesc.height;
        ambientOcclusionDesc.format = rhi::RHIFormat::R16_FLOAT;
        ambientOcclusionDesc.usage = rhi::RHITextureUsage::RenderTarget | rhi::RHITextureUsage::ShaderResource;
        ambientOcclusionDesc.debugName = "AmbientOcclusion";

        rhi::RHITextureDesc depthDesc{};
        depthDesc.width = backBufferDesc.width;
        depthDesc.height = backBufferDesc.height;
        depthDesc.format = rhi::RHIFormat::D32_FLOAT;
        depthDesc.usage = rhi::RHITextureUsage::DepthStencil | rhi::RHITextureUsage::ShaderResource;
        depthDesc.debugName = "SceneDepth";

        rhi::RHITextureDesc shadowMapDesc{};
        shadowMapDesc.width = 2048;
        shadowMapDesc.height = 2048;
        shadowMapDesc.format = rhi::RHIFormat::D32_FLOAT;
        shadowMapDesc.usage = rhi::RHITextureUsage::DepthStencil | rhi::RHITextureUsage::ShaderResource;
        shadowMapDesc.debugName = "DirectionalShadowMap";

        m_frameShadowMapHandle = m_frameGraph->CreateTransientTexture(shadowMapDesc);
        m_frameGBufferPositionHandle = m_frameGraph->CreateTransientTexture(gbufferDesc);
        m_frameGBufferNormalHandle = m_frameGraph->CreateTransientTexture(gbufferDesc);
        m_frameGBufferAlbedoHandle = m_frameGraph->CreateTransientTexture(gbufferDesc);
        m_frameAmbientOcclusionHandle = m_frameGraph->CreateTransientTexture(ambientOcclusionDesc);
        m_frameSceneDepthHandle = m_frameGraph->CreateTransientTexture(depthDesc);
        m_frameSceneColorHandle = m_frameGraph->CreateTransientTexture(sceneColorDesc);
        m_frameBokehDOFHandle = m_frameGraph->CreateTransientTexture(sceneColorDesc);
        m_shadowMapPass->ConfigureTarget(m_frameShadowMapHandle);
        m_gBufferPass->ConfigureTargets(m_frameGBufferPositionHandle, m_frameGBufferNormalHandle,
                                        m_frameGBufferAlbedoHandle, m_frameSceneDepthHandle);
        m_ssaoPass->ConfigureTargets(m_frameSceneDepthHandle, m_frameGBufferNormalHandle,
                                     m_frameAmbientOcclusionHandle);
        m_deferredLightingPass->ConfigureTargets(m_frameGBufferPositionHandle, m_frameGBufferNormalHandle,
                                                 m_frameGBufferAlbedoHandle, m_frameShadowMapHandle,
                                                 m_frameAmbientOcclusionHandle, m_frameSceneColorHandle);
        m_bokehDOFPass->Configure(m_frameSceneColorHandle, m_frameGBufferPositionHandle, m_frameBokehDOFHandle);
        m_toneMappingPass->Configure(m_frameBokehDOFHandle, m_frameBackBufferHandle);
        m_imguiPass->Configure(m_frameBackBufferHandle);
        m_shadowMapPass->SetSharedGeometry(m_frameSceneVertexBufferHandle, m_frameSceneIndexBufferHandle);
        m_shadowMapPass->SetSceneData(std::span<const render::StaticMeshDrawItem>{}, m_frameConstantsBufferHandle,
                                      m_frameMaterialBufferHandle);
        m_ssaoPass->SetFrameData(m_frameConstantsBufferHandle);
        m_deferredLightingPass->SetFrameData(m_frameConstantsBufferHandle);
        m_bokehDOFPass->SetFrameData(m_frameConstantsBufferHandle);
        m_deferredLightingPass->SetIBLTextures(m_frameIBLPrefilteredHandle, m_frameIBLIrradianceHandle,
                                               m_frameIBLBrdfLutHandle);
        m_gBufferPass->SetSceneData(std::span<const render::StaticMeshDrawItem>{}, m_frameConstantsBufferHandle,
                                    m_frameMaterialBufferHandle, m_frameSceneDrawBufferHandle);
        m_gBufferPass->SetSharedGeometry(m_frameSceneVertexBufferHandle, m_frameSceneIndexBufferHandle);
        if (m_gpuDrivenAvailable)
        {
            m_gBufferPass->SetIndirectBuffers(m_frameGPUDrivenIndirectArgsHandle, m_frameGPUDrivenIndirectCountHandle,
                                              m_frameSceneVertexBufferHandle, m_frameSceneIndexBufferHandle,
                                              m_sceneDrawCount);
        }
        else
        {
            m_gBufferPass->DisableIndirect();
        }
        m_frameGraph->AddPass(*m_shadowMapPass);
        if (m_gpuDrivenAvailable)
        {
            WEST_ASSERT(m_gpuDrivenCountResetPass != nullptr);
            WEST_ASSERT(m_gpuDrivenCullingPass != nullptr);
            WEST_ASSERT(m_gpuDrivenCountReadbackPass != nullptr);

            m_gpuDrivenCountResetPass->Configure(m_frameGPUDrivenCountResetBufferHandle,
                                                 m_frameGPUDrivenIndirectCountHandle, sizeof(uint32_t));
            m_gpuDrivenCullingPass->Configure(m_frameConstantsBufferHandle, m_frameSceneDrawBufferHandle,
                                              m_frameGPUDrivenIndirectArgsHandle, m_frameGPUDrivenIndirectCountHandle,
                                              m_sceneDrawCount);
            m_gpuDrivenCountReadbackPass->Configure(m_frameGPUDrivenIndirectCountHandle, m_frameGPUDrivenReadbackHandle,
                                                    sizeof(uint32_t));

            m_frameGraph->AddPass(*m_gpuDrivenCountResetPass);
            m_frameGraph->AddPass(*m_gpuDrivenCullingPass);
        }
        m_frameGraph->AddPass(*m_gBufferPass);
        if (m_gpuDrivenAvailable)
        {
            m_frameGraph->AddPass(*m_gpuDrivenCountReadbackPass);
        }
        m_frameGraph->AddPass(*m_ssaoPass);
        m_frameGraph->AddPass(*m_deferredLightingPass);
        m_frameGraph->AddPass(*m_bokehDOFPass);
        m_frameGraph->AddPass(*m_toneMappingPass);
        m_frameGraph->AddPass(*m_imguiPass);
        m_frameGraph->Compile();

        m_frameGraphWidth = backBufferDesc.width;
        m_frameGraphHeight = backBufferDesc.height;
        m_frameGraphBackBufferFormat = backBufferDesc.format;
        return;
    }

    m_frameGraph->UpdateImportedTexture(m_frameBackBufferHandle, backBuffer, initialState,
                                        rhi::RHIResourceState::Present, "SwapchainBackBuffer");
    m_frameGraph->UpdateImportedBuffer(m_frameConstantsBufferHandle, frameConstantsBuffer,
                                       rhi::RHIResourceState::ShaderResource, rhi::RHIResourceState::ShaderResource,
                                       "FrameBuffer");
    m_frameGraph->UpdateImportedBuffer(m_frameMaterialBufferHandle, m_materialBuffer.get(),
                                       rhi::RHIResourceState::ShaderResource, rhi::RHIResourceState::ShaderResource,
                                       "MaterialBuffer");
    m_frameGraph->UpdateImportedBuffer(m_frameSceneDrawBufferHandle, m_sceneDrawBuffer.get(),
                                       rhi::RHIResourceState::ShaderResource, rhi::RHIResourceState::ShaderResource,
                                       "SceneDrawBuffer");
    if (m_frameSceneVertexBufferHandle.IsValid())
    {
        m_frameGraph->UpdateImportedBuffer(m_frameSceneVertexBufferHandle, m_sceneVertexBuffer.get(),
                                           rhi::RHIResourceState::VertexBuffer, rhi::RHIResourceState::VertexBuffer,
                                           "SceneVertexBuffer");
    }
    if (m_frameSceneIndexBufferHandle.IsValid())
    {
        m_frameGraph->UpdateImportedBuffer(m_frameSceneIndexBufferHandle, m_sceneIndexBuffer.get(),
                                           rhi::RHIResourceState::IndexBuffer, rhi::RHIResourceState::IndexBuffer,
                                           "SceneIndexBuffer");
    }
    m_frameGraph->UpdateImportedTexture(m_frameIBLPrefilteredHandle, m_iblPrefilteredTexture.get(),
                                        rhi::RHIResourceState::ShaderResource, rhi::RHIResourceState::ShaderResource,
                                        "IBLPrefilteredEnvironment");
    m_frameGraph->UpdateImportedTexture(m_frameIBLIrradianceHandle, m_iblIrradianceTexture.get(),
                                        rhi::RHIResourceState::ShaderResource, rhi::RHIResourceState::ShaderResource,
                                        "IBLIrradianceEnvironment");
    m_frameGraph->UpdateImportedTexture(m_frameIBLBrdfLutHandle, m_iblBrdfLutTexture.get(),
                                        rhi::RHIResourceState::ShaderResource, rhi::RHIResourceState::ShaderResource,
                                        "IBLBrdfLut");

    if (m_gpuDrivenAvailable)
    {
        WEST_ASSERT(m_gpuDrivenCountResetBuffer != nullptr);
        WEST_ASSERT(indirectArgsBuffer != nullptr);
        WEST_ASSERT(indirectCountBuffer != nullptr);
        WEST_ASSERT(readbackBuffer != nullptr);

        m_frameGraph->UpdateImportedBuffer(m_frameGPUDrivenCountResetBufferHandle, m_gpuDrivenCountResetBuffer.get(),
                                           rhi::RHIResourceState::CopySource, rhi::RHIResourceState::CopySource,
                                           "GPUDrivenIndirectCountReset");
        m_frameGraph->UpdateImportedBuffer(m_frameGPUDrivenIndirectArgsHandle, indirectArgsBuffer,
                                           hasPreviousGPUDrivenState ? rhi::RHIResourceState::IndirectArgument
                                                                     : rhi::RHIResourceState::Undefined,
                                           rhi::RHIResourceState::IndirectArgument, "GPUDrivenIndirectArgs");
        m_frameGraph->UpdateImportedBuffer(m_frameGPUDrivenIndirectCountHandle, indirectCountBuffer,
                                           hasPreviousGPUDrivenState ? rhi::RHIResourceState::CopySource
                                                                     : rhi::RHIResourceState::Undefined,
                                           rhi::RHIResourceState::CopySource, "GPUDrivenIndirectCount");
        m_frameGraph->UpdateImportedBuffer(m_frameGPUDrivenReadbackHandle, readbackBuffer,
                                           rhi::RHIResourceState::CopyDest, rhi::RHIResourceState::CopyDest,
                                           "GPUDrivenVisibleCountReadback");
    }
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
    for (const auto& frameConstantsBuffer : m_frameConstantsBuffers)
    {
        if (frameConstantsBuffer && frameConstantsBuffer->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
        {
            m_rhiDevice->UnregisterBindlessResource(frameConstantsBuffer.get());
        }
    }
    if (m_materialBuffer && m_materialBuffer->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
    {
        m_rhiDevice->UnregisterBindlessResource(m_materialBuffer.get());
    }
    if (m_sceneDrawBuffer && m_sceneDrawBuffer->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
    {
        m_rhiDevice->UnregisterBindlessResource(m_sceneDrawBuffer.get());
    }
    for (const auto& indirectArgsBuffer : m_gpuDrivenIndirectArgsBuffers)
    {
        if (indirectArgsBuffer && indirectArgsBuffer->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
        {
            m_rhiDevice->UnregisterBindlessResource(indirectArgsBuffer.get());
        }
    }
    for (const auto& indirectCountBuffer : m_gpuDrivenIndirectCountBuffers)
    {
        if (indirectCountBuffer && indirectCountBuffer->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
        {
            m_rhiDevice->UnregisterBindlessResource(indirectCountBuffer.get());
        }
    }
    if (m_checkerSampler && m_checkerSampler->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
    {
        m_rhiDevice->UnregisterBindlessResource(m_checkerSampler.get());
    }
    if (m_materialStableSampler && m_materialStableSampler->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
    {
        m_rhiDevice->UnregisterBindlessResource(m_materialStableSampler.get());
    }
    if (m_shadowSampler && m_shadowSampler->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
    {
        m_rhiDevice->UnregisterBindlessResource(m_shadowSampler.get());
    }
    if (m_iblSampler && m_iblSampler->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
    {
        m_rhiDevice->UnregisterBindlessResource(m_iblSampler.get());
    }
    if (m_checkerTexture && m_checkerTexture->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
    {
        m_rhiDevice->UnregisterBindlessResource(m_checkerTexture.get());
    }
    if (m_iblPrefilteredTexture && m_iblPrefilteredTexture->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
    {
        m_rhiDevice->UnregisterBindlessResource(m_iblPrefilteredTexture.get());
    }
    if (m_iblIrradianceTexture && m_iblIrradianceTexture->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
    {
        m_rhiDevice->UnregisterBindlessResource(m_iblIrradianceTexture.get());
    }
    if (m_iblBrdfLutTexture && m_iblBrdfLutTexture->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
    {
        m_rhiDevice->UnregisterBindlessResource(m_iblBrdfLutTexture.get());
    }
    for (const SceneTextureResource& textureResource : m_sceneTextureResources)
    {
        if (textureResource.texture && textureResource.texture->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
        {
            m_rhiDevice->UnregisterBindlessResource(textureResource.texture.get());
        }
    }

    if (m_transientResourcePool)
    {
        m_transientResourcePool->Reset(m_rhiDevice.get());
    }

    if (m_commandListPool)
    {
        m_commandListPool->Reset();
    }

    m_imguiPass.reset();
    m_imguiRenderer.reset();
    m_gpuTimerManager.reset();
    m_frameTelemetry.reset();
    m_toneMappingPass.reset();
    m_bokehDOFPass.reset();
    m_ssaoPass.reset();
    m_deferredLightingPass.reset();
    m_gpuDrivenCountReadbackPass.reset();
    m_gpuDrivenCountResetPass.reset();
    m_gpuDrivenCullingPass.reset();
    m_gBufferPass.reset();
    m_shadowMapPass.reset();
    m_frameConstantsBuffers.clear();
    m_materialBuffer.reset();
    m_sceneDrawBuffer.reset();
    m_gpuDrivenCountResetBuffer.reset();
    m_gpuDrivenIndirectArgsBuffers.clear();
    m_gpuDrivenIndirectCountBuffers.clear();
    m_gpuDrivenCountReadbackBuffers.clear();
    m_gpuDrivenReadbackPending.clear();
    m_iblBrdfLutTexture.reset();
    m_iblIrradianceTexture.reset();
    m_iblPrefilteredTexture.reset();
    m_sceneTextureResources.clear();
    m_sceneMeshResources.clear();
    m_sceneIndexBuffer.reset();
    m_sceneVertexBuffer.reset();
    m_sceneCamera.reset();
    m_sceneAsset.reset();
    m_frameGraph.reset();
    m_commandListPool.reset();
    m_transientResourcePool.reset();
    m_psoCache.reset();
    m_iblSampler.reset();
    m_shadowSampler.reset();
    m_materialStableSampler.reset();
    m_checkerSampler.reset();
    m_checkerTexture.reset();
    m_quadIB.reset();
    m_quadVB.reset();
    m_presentSemaphores.clear();
    m_acquireSemaphores.clear();
    m_frameFence.reset();
    m_swapChain.reset();
    m_rhiDevice.reset();
    m_sceneDrawCount = 0;
    m_lastGPUDrivenVisibleCount = 0;
    m_gpuDrivenAvailable = false;
    m_gpuDrivenVisibilityLogged = false;

    WEST_LOG_INFO(LogCategory::RHI, "RHI shutdown complete.");
}

// ── Application Factory ──────────────────────────────────────────────────
std::unique_ptr<IApplication> CreateApplication(const ApplicationDesc& desc)
{
    return std::make_unique<Win32Application>(desc);
}

std::unique_ptr<IApplication> CreateApplication()
{
    return CreateApplication(ApplicationDesc{});
}

} // namespace west
