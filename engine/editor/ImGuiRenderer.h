// =============================================================================
// WestEngine - Editor
// Dear ImGui renderer bootstrap for runtime tooling
// =============================================================================
#pragma once

#include "core/Types.h"
#include "rhi/interface/RHIEnums.h"

#include <memory>

namespace west::rhi
{
class IRHICommandList;
class IRHIDevice;
} // namespace west::rhi

namespace west::shader
{
class PSOCache;
} // namespace west::shader

namespace west::editor
{

class ImGuiRenderer final
{
public:
    struct InputState
    {
        float deltaSeconds = 1.0f / 60.0f;
        float displayWidth = 0.0f;
        float displayHeight = 0.0f;
        float mouseX = 0.0f;
        float mouseY = 0.0f;
        bool mouseInsideWindow = false;
        bool mouseButtons[3] = {};
    };

    ImGuiRenderer(rhi::IRHIDevice& device, shader::PSOCache& psoCache, rhi::RHIBackend backend,
                  uint32 maxFramesInFlight);
    ~ImGuiRenderer();

    void BeginFrame(const InputState& input);
    void EndFrame(uint32 frameIndex);
    void Render(rhi::IRHICommandList& commandList, uint32 targetWidth, uint32 targetHeight);

    [[nodiscard]] bool HasDrawData() const;
    [[nodiscard]] bool WantsMouseCapture() const;
    [[nodiscard]] bool WantsKeyboardCapture() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace west::editor
