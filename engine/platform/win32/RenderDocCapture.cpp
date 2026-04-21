// =============================================================================
// WestEngine - Platform (Win32)
// Optional RenderDoc in-application capture helper
// =============================================================================
#include "platform/win32/RenderDocCapture.h"

#include "core/Logger.h"
#include "platform/win32/Win32Headers.h"

namespace west
{

namespace
{

using RenderDocDevicePointer = void*;
using RenderDocWindowHandle = void*;

using pRENDERDOC_GetAPIVersion = void(__cdecl*)(int* major, int* minor, int* patch);
using pRENDERDOC_SetCaptureOptionU32 = int(__cdecl*)(int option, uint32_t value);
using pRENDERDOC_SetCaptureOptionF32 = int(__cdecl*)(int option, float value);
using pRENDERDOC_GetCaptureOptionU32 = uint32_t(__cdecl*)(int option);
using pRENDERDOC_GetCaptureOptionF32 = float(__cdecl*)(int option);
using pRENDERDOC_SetFocusToggleKeys = void(__cdecl*)(const int* keys, int numKeys);
using pRENDERDOC_SetCaptureKeys = void(__cdecl*)(const int* keys, int numKeys);
using pRENDERDOC_GetOverlayBits = uint32_t(__cdecl*)();
using pRENDERDOC_MaskOverlayBits = void(__cdecl*)(uint32_t andMask, uint32_t orMask);
using pRENDERDOC_RemoveHooks = void(__cdecl*)();
using pRENDERDOC_UnloadCrashHandler = void(__cdecl*)();
using pRENDERDOC_SetCaptureFilePathTemplate = void(__cdecl*)(const char* pathTemplate);
using pRENDERDOC_GetCaptureFilePathTemplate = const char*(__cdecl*)();
using pRENDERDOC_GetNumCaptures = uint32_t(__cdecl*)();
using pRENDERDOC_GetCapture = uint32_t(__cdecl*)(uint32_t index, char* filename, uint32_t* pathLength,
                                                 uint64_t* timestamp);
using pRENDERDOC_TriggerCapture = void(__cdecl*)();
using pRENDERDOC_IsTargetControlConnected = uint32_t(__cdecl*)();
using pRENDERDOC_LaunchReplayUI = uint32_t(__cdecl*)(uint32_t connectTargetControl, const char* cmdLine);
using pRENDERDOC_SetActiveWindow = void(__cdecl*)(RenderDocDevicePointer device, RenderDocWindowHandle window);
using pRENDERDOC_StartFrameCapture = void(__cdecl*)(RenderDocDevicePointer device, RenderDocWindowHandle window);
using pRENDERDOC_IsFrameCapturing = uint32_t(__cdecl*)();
using pRENDERDOC_EndFrameCapture = uint32_t(__cdecl*)(RenderDocDevicePointer device, RenderDocWindowHandle window);

struct RenderDocAPI_1_0_0
{
    pRENDERDOC_GetAPIVersion GetAPIVersion = nullptr;
    pRENDERDOC_SetCaptureOptionU32 SetCaptureOptionU32 = nullptr;
    pRENDERDOC_SetCaptureOptionF32 SetCaptureOptionF32 = nullptr;
    pRENDERDOC_GetCaptureOptionU32 GetCaptureOptionU32 = nullptr;
    pRENDERDOC_GetCaptureOptionF32 GetCaptureOptionF32 = nullptr;
    pRENDERDOC_SetFocusToggleKeys SetFocusToggleKeys = nullptr;
    pRENDERDOC_SetCaptureKeys SetCaptureKeys = nullptr;
    pRENDERDOC_GetOverlayBits GetOverlayBits = nullptr;
    pRENDERDOC_MaskOverlayBits MaskOverlayBits = nullptr;
    pRENDERDOC_RemoveHooks RemoveHooks = nullptr;
    pRENDERDOC_UnloadCrashHandler UnloadCrashHandler = nullptr;
    pRENDERDOC_SetCaptureFilePathTemplate SetCaptureFilePathTemplate = nullptr;
    pRENDERDOC_GetCaptureFilePathTemplate GetCaptureFilePathTemplate = nullptr;
    pRENDERDOC_GetNumCaptures GetNumCaptures = nullptr;
    pRENDERDOC_GetCapture GetCapture = nullptr;
    pRENDERDOC_TriggerCapture TriggerCapture = nullptr;
    pRENDERDOC_IsTargetControlConnected IsTargetControlConnected = nullptr;
    pRENDERDOC_LaunchReplayUI LaunchReplayUI = nullptr;
    pRENDERDOC_SetActiveWindow SetActiveWindow = nullptr;
    pRENDERDOC_StartFrameCapture StartFrameCapture = nullptr;
    pRENDERDOC_IsFrameCapturing IsFrameCapturing = nullptr;
    pRENDERDOC_EndFrameCapture EndFrameCapture = nullptr;
};

using pRENDERDOC_GetAPI = int(__cdecl*)(int version, void** outAPIPointers);
constexpr int kRenderDocAPIVersion_1_0_0 = 10000;

} // namespace

struct RenderDocCapture::API : RenderDocAPI_1_0_0
{
};

void RenderDocCapture::Initialize()
{
    HMODULE renderDocModule = ::GetModuleHandleW(L"renderdoc.dll");
    if (!renderDocModule)
    {
        renderDocModule = ::GetModuleHandleW(L"renderdoc");
    }

    if (!renderDocModule)
    {
        return;
    }

    const auto getApi =
        reinterpret_cast<pRENDERDOC_GetAPI>(::GetProcAddress(renderDocModule, "RENDERDOC_GetAPI"));
    if (!getApi)
    {
        WEST_LOG_WARNING(LogCategory::Core, "RenderDoc module is loaded but RENDERDOC_GetAPI was not found.");
        return;
    }

    static API api{};
    void* apiPointer = &api;
    if (getApi(kRenderDocAPIVersion_1_0_0, &apiPointer) == 0)
    {
        WEST_LOG_WARNING(LogCategory::Core, "RenderDoc API 1.0.0 is unavailable in the injected module.");
        return;
    }

    m_api = static_cast<API*>(apiPointer);
    WEST_LOG_INFO(LogCategory::Core, "RenderDoc capture API detected.");
}

void RenderDocCapture::BeginFrameCapture(const char* captureTitle)
{
    if (!m_api || !m_api->StartFrameCapture)
    {
        return;
    }

    if (captureTitle && m_api->SetCaptureFilePathTemplate)
    {
        // renderdoccmd --capture-file already controls the capture path, so leave it untouched.
    }

    m_api->StartFrameCapture(nullptr, nullptr);
}

bool RenderDocCapture::EndFrameCapture()
{
    if (!m_api || !m_api->EndFrameCapture)
    {
        return false;
    }

    return m_api->EndFrameCapture(nullptr, nullptr) != 0;
}

} // namespace west
