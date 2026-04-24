// =============================================================================
// WestEngine - Editor
// Dear ImGui renderer bootstrap for runtime tooling
// =============================================================================
#include "editor/ImGuiRenderer.h"

#include "core/Assert.h"
#include "core/Logger.h"
#include "generated/ShaderMetadata.h"
#include "rhi/interface/IRHIBuffer.h"
#include "rhi/interface/IRHICommandList.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHIFence.h"
#include "rhi/interface/IRHIPipeline.h"
#include "rhi/interface/IRHIQueue.h"
#include "rhi/interface/IRHISampler.h"
#include "rhi/interface/IRHITexture.h"
#include "shader/PSOCache.h"
#include "shader/ShaderCompiler.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

namespace west::editor
{

namespace
{

struct DescriptorHandle
{
    rhi::BindlessIndex index = rhi::kInvalidBindlessIndex;
    uint32_t unused = 0;
};

struct ImGuiPushConstants
{
    float scale[2] = {};
    float translate[2] = {};
    DescriptorHandle texture;
    DescriptorHandle sampler;
};

static_assert(sizeof(ImGuiPushConstants) == shader::metadata::ImGui::PushConstantSizeBytes);

[[nodiscard]] uint32_t AlignUp(uint32_t value, uint32_t alignment)
{
    WEST_ASSERT(alignment > 0);
    return (value + alignment - 1u) & ~(alignment - 1u);
}

[[nodiscard]] rhi::RHIBlendAttachment MakeAlphaBlendAttachment()
{
    rhi::RHIBlendAttachment attachment{};
    attachment.blendEnable = true;
    attachment.srcColor = rhi::RHIBlendFactor::SrcAlpha;
    attachment.dstColor = rhi::RHIBlendFactor::OneMinusSrcAlpha;
    attachment.colorOp = rhi::RHIBlendOp::Add;
    attachment.srcAlpha = rhi::RHIBlendFactor::One;
    attachment.dstAlpha = rhi::RHIBlendFactor::OneMinusSrcAlpha;
    attachment.alphaOp = rhi::RHIBlendOp::Add;
    return attachment;
}

void ApplyRuntimeStyle()
{
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 8.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.FrameBorderSize = 1.0f;
    style.WindowBorderSize = 1.0f;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.05f, 0.06f, 0.08f, 0.94f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.18f, 0.22f, 0.98f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.20f, 0.30f, 0.36f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.18f, 0.30f, 0.36f, 0.75f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.27f, 0.45f, 0.52f, 0.86f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.50f, 0.58f, 0.95f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.32f, 0.38f, 0.82f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.28f, 0.46f, 0.54f, 0.92f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.34f, 0.56f, 0.66f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.10f, 0.12f, 0.15f, 0.90f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.18f, 0.24f, 0.28f, 0.96f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.22f, 0.30f, 0.35f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.92f, 0.74f, 0.28f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.90f, 0.70f, 0.24f, 0.85f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.98f, 0.82f, 0.34f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.33f, 0.38f, 1.0f);
}

} // namespace

struct ImGuiRenderer::Impl
{
    struct FrameResources
    {
        std::unique_ptr<rhi::IRHIBuffer> vertexBuffer;
        std::unique_ptr<rhi::IRHIBuffer> indexBuffer;
        uint32_t vertexCapacity = 0;
        uint32_t indexCapacity = 0;
    };

    rhi::IRHIDevice& device;
    shader::PSOCache& psoCache;
    rhi::RHIBackend backend = rhi::RHIBackend::DX12;
    std::vector<FrameResources> frames;
    std::unique_ptr<rhi::IRHITexture> fontTexture;
    std::unique_ptr<rhi::IRHISampler> fontSampler;
    rhi::IRHIPipeline* pipeline = nullptr;
    ImGuiContext* context = nullptr;
    uint32_t activeFrameIndex = 0;
    bool hasDrawData = false;
    bool wantsMouseCapture = false;
    bool wantsKeyboardCapture = false;

    Impl(rhi::IRHIDevice& inDevice, shader::PSOCache& inPsoCache, rhi::RHIBackend inBackend,
         uint32_t maxFramesInFlight)
        : device(inDevice)
        , psoCache(inPsoCache)
        , backend(inBackend)
        , frames(maxFramesInFlight)
    {
        context = ImGui::CreateContext();
        WEST_CHECK(context != nullptr, "Failed to create Dear ImGui context");
        ImGui::SetCurrentContext(context);

        ImGuiIO& io = ImGui::GetIO();
        io.BackendPlatformName = "WestEngine.Win32.Manual";
        io.BackendRendererName = "WestEngine.RHI.ImGui";
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        io.Fonts->AddFontDefault();
        ApplyRuntimeStyle();

        CreateFontResources();
        CreatePipeline();
    }

    ~Impl()
    {
        if (fontSampler && fontSampler->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
        {
            device.UnregisterBindlessResource(fontSampler->GetBindlessIndex());
        }
        if (fontTexture && fontTexture->GetBindlessIndex() != rhi::kInvalidBindlessIndex)
        {
            device.UnregisterBindlessResource(fontTexture->GetBindlessIndex());
        }
        if (context != nullptr)
        {
            ImGui::DestroyContext(context);
            context = nullptr;
        }
    }

    void CreateFontResources()
    {
        ImGui::SetCurrentContext(context);
        ImGuiIO& io = ImGui::GetIO();

        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        WEST_CHECK(pixels != nullptr && width > 0 && height > 0, "Failed to build Dear ImGui font atlas");

        const uint32_t bytesPerPixel = 4u;
        const uint32_t rowPitchBytes = static_cast<uint32_t>(width) * bytesPerPixel;
        const uint32_t alignedRowPitchBytes = AlignUp(rowPitchBytes, 256u);
        const uint32_t stagingSizeBytes = alignedRowPitchBytes * static_cast<uint32_t>(height);

        rhi::RHIBufferDesc stagingDesc{};
        stagingDesc.sizeBytes = stagingSizeBytes;
        stagingDesc.usage = rhi::RHIBufferUsage::CopySource;
        stagingDesc.memoryType = rhi::RHIMemoryType::Upload;
        stagingDesc.debugName = "ImGuiFontAtlasStaging";
        auto stagingBuffer = device.CreateBuffer(stagingDesc);
        WEST_CHECK(stagingBuffer != nullptr, "Failed to create Dear ImGui staging buffer");

        auto* mapped = static_cast<uint8_t*>(stagingBuffer->Map());
        WEST_CHECK(mapped != nullptr, "Failed to map Dear ImGui staging buffer");
        for (int y = 0; y < height; ++y)
        {
            const uint8_t* sourceRow = pixels + (static_cast<size_t>(y) * static_cast<size_t>(rowPitchBytes));
            std::memcpy(mapped + (static_cast<size_t>(y) * static_cast<size_t>(alignedRowPitchBytes)), sourceRow,
                        rowPitchBytes);
        }
        stagingBuffer->Unmap();

        rhi::RHITextureDesc textureDesc{};
        textureDesc.width = static_cast<uint32_t>(width);
        textureDesc.height = static_cast<uint32_t>(height);
        textureDesc.format = rhi::RHIFormat::RGBA8_UNORM;
        textureDesc.usage = rhi::RHITextureUsage::ShaderResource | rhi::RHITextureUsage::CopyDest;
        textureDesc.debugName = "ImGuiFontAtlas";
        fontTexture = device.CreateTexture(textureDesc);
        WEST_CHECK(fontTexture != nullptr, "Failed to create Dear ImGui font texture");

        rhi::RHISamplerDesc samplerDesc{};
        samplerDesc.minFilter = rhi::RHIFilter::Linear;
        samplerDesc.magFilter = rhi::RHIFilter::Linear;
        samplerDesc.mipmapMode = rhi::RHIMipmapMode::Linear;
        samplerDesc.addressU = rhi::RHIAddressMode::ClampToEdge;
        samplerDesc.addressV = rhi::RHIAddressMode::ClampToEdge;
        samplerDesc.addressW = rhi::RHIAddressMode::ClampToEdge;
        samplerDesc.anisotropyEnable = false;
        samplerDesc.debugName = "ImGuiFontSampler";
        fontSampler = device.CreateSampler(samplerDesc);
        WEST_CHECK(fontSampler != nullptr, "Failed to create Dear ImGui font sampler");

        WEST_CHECK(device.RegisterBindlessResource(fontTexture.get()) != rhi::kInvalidBindlessIndex,
                   "Failed to register Dear ImGui font texture");
        WEST_CHECK(device.RegisterBindlessResource(fontSampler.get()) != rhi::kInvalidBindlessIndex,
                   "Failed to register Dear ImGui font sampler");

        auto uploadCommandList = device.CreateCommandList(rhi::RHIQueueType::Graphics);
        WEST_CHECK(uploadCommandList != nullptr, "Failed to create Dear ImGui upload command list");
        uploadCommandList->Begin();

        rhi::RHIBarrierDesc toCopyBarrier{};
        toCopyBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
        toCopyBarrier.texture = fontTexture.get();
        toCopyBarrier.stateBefore = rhi::RHIResourceState::Undefined;
        toCopyBarrier.stateAfter = rhi::RHIResourceState::CopyDest;
        uploadCommandList->ResourceBarrier(toCopyBarrier);

        rhi::RHICopyRegion copyRegion{};
        copyRegion.bufferOffset = 0;
        copyRegion.bufferRowLength = alignedRowPitchBytes / bytesPerPixel;
        copyRegion.bufferImageHeight = static_cast<uint32_t>(height);
        copyRegion.texWidth = static_cast<uint32_t>(width);
        copyRegion.texHeight = static_cast<uint32_t>(height);
        copyRegion.texDepth = 1;
        uploadCommandList->CopyBufferToTexture(stagingBuffer.get(), fontTexture.get(), copyRegion);

        rhi::RHIBarrierDesc toShaderBarrier{};
        toShaderBarrier.type = rhi::RHIBarrierDesc::Type::Transition;
        toShaderBarrier.texture = fontTexture.get();
        toShaderBarrier.stateBefore = rhi::RHIResourceState::CopyDest;
        toShaderBarrier.stateAfter = rhi::RHIResourceState::ShaderResource;
        uploadCommandList->ResourceBarrier(toShaderBarrier);
        uploadCommandList->End();

        auto uploadFence = device.CreateFence(0);
        WEST_CHECK(uploadFence != nullptr, "Failed to create Dear ImGui upload fence");

        std::vector<rhi::IRHICommandList*> commandLists = {uploadCommandList.get()};
        std::vector<rhi::RHITimelineSignalDesc> signals = {{uploadFence.get(), 1}};
        rhi::RHISubmitInfo submitInfo{};
        submitInfo.commandLists = std::span<rhi::IRHICommandList* const>(commandLists.data(), commandLists.size());
        submitInfo.timelineSignals = std::span<const rhi::RHITimelineSignalDesc>(signals.data(), signals.size());
        device.GetQueue(rhi::RHIQueueType::Graphics)->Submit(submitInfo);
        uploadFence->Wait(1);

    }

    void CreatePipeline()
    {
        std::vector<uint8_t> vertexShader;
        std::vector<uint8_t> fragmentShader;

        if (backend == rhi::RHIBackend::DX12)
        {
            WEST_CHECK(shader::ShaderCompiler::LoadBytecode("ImGui.vs.dxil", vertexShader),
                       "Failed to load ImGui DXIL vertex shader");
            WEST_CHECK(shader::ShaderCompiler::LoadBytecode("ImGui.ps.dxil", fragmentShader),
                       "Failed to load ImGui DXIL fragment shader");
        }
        else
        {
            WEST_CHECK(shader::ShaderCompiler::LoadBytecode("ImGui.vs.spv", vertexShader),
                       "Failed to load ImGui SPIR-V vertex shader");
            WEST_CHECK(shader::ShaderCompiler::LoadBytecode("ImGui.ps.spv", fragmentShader),
                       "Failed to load ImGui SPIR-V fragment shader");
        }

        constexpr std::array<rhi::RHIVertexAttribute, 3> vertexAttributes = {{
            {"POSITION", rhi::RHIFormat::RG32_FLOAT, 0},
            {"TEXCOORD", rhi::RHIFormat::RG32_FLOAT, 8},
            {"COLOR", rhi::RHIFormat::RGBA8_UNORM, 16},
        }};
        const rhi::RHIFormat colorFormat = rhi::RHIFormat::BGRA8_UNORM;
        const rhi::RHIBlendAttachment blendAttachment = MakeAlphaBlendAttachment();

        rhi::RHIGraphicsPipelineDesc pipelineDesc{};
        pipelineDesc.vertexShader = std::span<const uint8_t>(vertexShader.data(), vertexShader.size());
        pipelineDesc.fragmentShader = std::span<const uint8_t>(fragmentShader.data(), fragmentShader.size());
        pipelineDesc.vertexAttributes = vertexAttributes;
        pipelineDesc.vertexStride = 20;
        pipelineDesc.topology = rhi::RHIPrimitiveTopology::TriangleList;
        pipelineDesc.cullMode = rhi::RHICullMode::None;
        pipelineDesc.depthTest = false;
        pipelineDesc.depthWrite = false;
        pipelineDesc.colorFormats = {&colorFormat, 1};
        pipelineDesc.depthFormat = rhi::RHIFormat::Unknown;
        pipelineDesc.blendAttachments = {&blendAttachment, 1};
        pipelineDesc.pushConstantSizeBytes = shader::metadata::ImGui::PushConstantSizeBytes;
        pipelineDesc.debugName = "ImGuiPipeline";

        pipeline = psoCache.GetOrCreateGraphicsPipeline(device, pipelineDesc);
        WEST_ASSERT(pipeline != nullptr);
    }

    void EnsureFrameCapacity(FrameResources& frame, uint32_t vertexCount, uint32_t indexCount)
    {
        if (!frame.vertexBuffer || vertexCount > frame.vertexCapacity)
        {
            frame.vertexCapacity = std::max<uint32_t>(vertexCount + (vertexCount / 2u), 1024u);
            rhi::RHIBufferDesc vertexDesc{};
            vertexDesc.sizeBytes = static_cast<uint64_t>(frame.vertexCapacity) * sizeof(ImDrawVert);
            vertexDesc.structureByteStride = sizeof(ImDrawVert);
            vertexDesc.usage = rhi::RHIBufferUsage::VertexBuffer;
            vertexDesc.memoryType = rhi::RHIMemoryType::Upload;
            vertexDesc.debugName = "ImGuiVertexBuffer";
            frame.vertexBuffer = device.CreateBuffer(vertexDesc);
            WEST_CHECK(frame.vertexBuffer != nullptr, "Failed to create Dear ImGui vertex buffer");
        }

        if (!frame.indexBuffer || indexCount > frame.indexCapacity)
        {
            frame.indexCapacity = std::max<uint32_t>(indexCount + (indexCount / 2u), 2048u);
            rhi::RHIBufferDesc indexDesc{};
            indexDesc.sizeBytes = static_cast<uint64_t>(frame.indexCapacity) * sizeof(uint32_t);
            indexDesc.structureByteStride = sizeof(uint32_t);
            indexDesc.usage = rhi::RHIBufferUsage::IndexBuffer;
            indexDesc.memoryType = rhi::RHIMemoryType::Upload;
            indexDesc.debugName = "ImGuiIndexBuffer";
            frame.indexBuffer = device.CreateBuffer(indexDesc);
            WEST_CHECK(frame.indexBuffer != nullptr, "Failed to create Dear ImGui index buffer");
        }
    }

    void UploadDrawData(uint32_t frameIndex)
    {
        ImGui::SetCurrentContext(context);
        ImDrawData* drawData = ImGui::GetDrawData();
        hasDrawData = drawData != nullptr && drawData->CmdListsCount > 0 && drawData->TotalVtxCount > 0 &&
                      drawData->TotalIdxCount > 0;
        activeFrameIndex = frameIndex % static_cast<uint32_t>(frames.size());
        if (!hasDrawData)
        {
            return;
        }

        FrameResources& frame = frames[activeFrameIndex];
        EnsureFrameCapacity(frame, static_cast<uint32_t>(drawData->TotalVtxCount),
                            static_cast<uint32_t>(drawData->TotalIdxCount));

        auto* vertexDestination = static_cast<ImDrawVert*>(frame.vertexBuffer->Map());
        auto* indexDestination = static_cast<uint32_t*>(frame.indexBuffer->Map());
        WEST_CHECK(vertexDestination != nullptr && indexDestination != nullptr,
                   "Failed to map Dear ImGui frame buffers");

        for (int drawListIndex = 0; drawListIndex < drawData->CmdListsCount; ++drawListIndex)
        {
            const ImDrawList* drawList = drawData->CmdLists[drawListIndex];
            std::memcpy(vertexDestination, drawList->VtxBuffer.Data,
                        static_cast<size_t>(drawList->VtxBuffer.Size) * sizeof(ImDrawVert));

            for (int index = 0; index < drawList->IdxBuffer.Size; ++index)
            {
                indexDestination[index] = static_cast<uint32_t>(drawList->IdxBuffer[index]);
            }

            vertexDestination += drawList->VtxBuffer.Size;
            indexDestination += drawList->IdxBuffer.Size;
        }

        frame.vertexBuffer->Unmap();
        frame.indexBuffer->Unmap();
    }
};

ImGuiRenderer::ImGuiRenderer(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                             uint32 maxFramesInFlight)
    : m_impl(std::make_unique<Impl>(device, psoCache, backend, maxFramesInFlight))
{
}

ImGuiRenderer::~ImGuiRenderer() = default;

void ImGuiRenderer::BeginFrame(const InputState& input)
{
    WEST_ASSERT(m_impl != nullptr);

    ImGui::SetCurrentContext(m_impl->context);
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(std::max(input.displayWidth, 1.0f), std::max(input.displayHeight, 1.0f));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = std::max(input.deltaSeconds, 1.0f / 1000.0f);
    io.MouseWheel = 0.0f;
    io.MouseWheelH = 0.0f;
    io.MousePos =
        input.mouseInsideWindow ? ImVec2(input.mouseX, input.mouseY) : ImVec2(-FLT_MAX, -FLT_MAX);
    io.MouseDown[0] = input.mouseButtons[0];
    io.MouseDown[1] = input.mouseButtons[1];
    io.MouseDown[2] = input.mouseButtons[2];
    ImGui::NewFrame();

    m_impl->wantsMouseCapture = io.WantCaptureMouse;
    m_impl->wantsKeyboardCapture = io.WantCaptureKeyboard;
}

void ImGuiRenderer::EndFrame(uint32 frameIndex)
{
    WEST_ASSERT(m_impl != nullptr);

    ImGui::SetCurrentContext(m_impl->context);
    ImGui::Render();
    ImGuiIO& io = ImGui::GetIO();
    m_impl->wantsMouseCapture = io.WantCaptureMouse;
    m_impl->wantsKeyboardCapture = io.WantCaptureKeyboard;
    m_impl->UploadDrawData(frameIndex);
}

void ImGuiRenderer::Render(rhi::IRHICommandList& commandList, uint32 targetWidth, uint32 targetHeight)
{
    WEST_ASSERT(m_impl != nullptr);
    if (!m_impl->hasDrawData)
    {
        return;
    }

    ImGui::SetCurrentContext(m_impl->context);
    ImDrawData* drawData = ImGui::GetDrawData();
    WEST_ASSERT(drawData != nullptr);

    Impl::FrameResources& frame = m_impl->frames[m_impl->activeFrameIndex];
    WEST_ASSERT(frame.vertexBuffer != nullptr);
    WEST_ASSERT(frame.indexBuffer != nullptr);
    WEST_ASSERT(m_impl->fontTexture != nullptr);
    WEST_ASSERT(m_impl->fontSampler != nullptr);
    WEST_ASSERT(m_impl->pipeline != nullptr);

    commandList.SetViewport(0.0f, 0.0f, static_cast<float>(targetWidth), static_cast<float>(targetHeight));
    commandList.SetScissor(0, 0, targetWidth, targetHeight);
    commandList.SetPipeline(m_impl->pipeline);
    commandList.SetVertexBuffer(0, frame.vertexBuffer.get());
    commandList.SetIndexBuffer(frame.indexBuffer.get(), rhi::RHIFormat::R32_UINT);

    ImGuiPushConstants pushConstants{};
    pushConstants.scale[0] = 2.0f / drawData->DisplaySize.x;
    pushConstants.scale[1] = -2.0f / drawData->DisplaySize.y;
    pushConstants.translate[0] = -1.0f - (drawData->DisplayPos.x * pushConstants.scale[0]);
    pushConstants.translate[1] = 1.0f - (drawData->DisplayPos.y * pushConstants.scale[1]);
    pushConstants.texture.index = m_impl->fontTexture->GetBindlessIndex();
    pushConstants.sampler.index = m_impl->fontSampler->GetBindlessIndex();
    commandList.SetPushConstants(&pushConstants, sizeof(pushConstants));

    int32_t vertexOffset = 0;
    uint32_t indexOffset = 0;
    const ImVec2 clipOffset = drawData->DisplayPos;
    const ImVec2 clipScale = drawData->FramebufferScale;

    for (int drawListIndex = 0; drawListIndex < drawData->CmdListsCount; ++drawListIndex)
    {
        const ImDrawList* drawList = drawData->CmdLists[drawListIndex];
        for (const ImDrawCmd& drawCommand : drawList->CmdBuffer)
        {
            ImVec2 clipMin((drawCommand.ClipRect.x - clipOffset.x) * clipScale.x,
                           (drawCommand.ClipRect.y - clipOffset.y) * clipScale.y);
            ImVec2 clipMax((drawCommand.ClipRect.z - clipOffset.x) * clipScale.x,
                           (drawCommand.ClipRect.w - clipOffset.y) * clipScale.y);

            clipMin.x = std::max(clipMin.x, 0.0f);
            clipMin.y = std::max(clipMin.y, 0.0f);
            clipMax.x = std::min(clipMax.x, static_cast<float>(targetWidth));
            clipMax.y = std::min(clipMax.y, static_cast<float>(targetHeight));
            if (clipMax.x <= clipMin.x || clipMax.y <= clipMin.y)
            {
                indexOffset += drawCommand.ElemCount;
                continue;
            }

            commandList.SetScissor(static_cast<int32_t>(clipMin.x), static_cast<int32_t>(clipMin.y),
                                   static_cast<uint32_t>(clipMax.x - clipMin.x),
                                   static_cast<uint32_t>(clipMax.y - clipMin.y));
            commandList.DrawIndexed(drawCommand.ElemCount, 1, indexOffset, vertexOffset, 0);
            indexOffset += drawCommand.ElemCount;
        }

        vertexOffset += drawList->VtxBuffer.Size;
    }
}

bool ImGuiRenderer::HasDrawData() const
{
    WEST_ASSERT(m_impl != nullptr);
    return m_impl->hasDrawData;
}

bool ImGuiRenderer::WantsMouseCapture() const
{
    WEST_ASSERT(m_impl != nullptr);
    return m_impl->wantsMouseCapture;
}

bool ImGuiRenderer::WantsKeyboardCapture() const
{
    WEST_ASSERT(m_impl != nullptr);
    return m_impl->wantsKeyboardCapture;
}

} // namespace west::editor
