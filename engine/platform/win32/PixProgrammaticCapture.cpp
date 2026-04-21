// =============================================================================
// WestEngine - Platform (Win32)
// Optional PIX programmatic GPU capture helper
// =============================================================================
#include "platform/win32/PixProgrammaticCapture.h"

#include "core/Logger.h"
#include "platform/win32/Win32Headers.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace west
{

namespace
{

constexpr unsigned long kPixCaptureGpu = 1ul << 1;

union PIXCaptureParameters
{
    struct GpuCaptureParametersT
    {
        PCWSTR FileName;
    } GpuCaptureParameters;
};

std::vector<int> ParseVersionComponents(const std::wstring& versionText)
{
    std::vector<int> components;
    std::wstring current;

    for (const wchar_t ch : versionText)
    {
        if (ch == L'.')
        {
            if (!current.empty())
            {
                components.push_back(std::stoi(current));
                current.clear();
            }
            continue;
        }

        if (ch < L'0' || ch > L'9')
        {
            return {};
        }

        current.push_back(ch);
    }

    if (!current.empty())
    {
        components.push_back(std::stoi(current));
    }

    return components;
}

bool IsVersionDirectory(const std::filesystem::path& candidate)
{
    const std::wstring name = candidate.filename().wstring();
    return !ParseVersionComponents(name).empty();
}

bool CompareVersionDirectoryNames(const std::filesystem::path& lhs, const std::filesystem::path& rhs)
{
    const std::vector<int> lhsComponents = ParseVersionComponents(lhs.filename().wstring());
    const std::vector<int> rhsComponents = ParseVersionComponents(rhs.filename().wstring());
    const size_t count = std::max(lhsComponents.size(), rhsComponents.size());

    for (size_t index = 0; index < count; ++index)
    {
        const int lhsValue = index < lhsComponents.size() ? lhsComponents[index] : 0;
        const int rhsValue = index < rhsComponents.size() ? rhsComponents[index] : 0;
        if (lhsValue != rhsValue)
        {
            return lhsValue < rhsValue;
        }
    }

    return lhs.filename().wstring() < rhs.filename().wstring();
}

std::filesystem::path GetProgramFilesDirectory()
{
    wchar_t buffer[MAX_PATH] = {};
    const DWORD length = ::GetEnvironmentVariableW(L"ProgramFiles", buffer, static_cast<DWORD>(std::size(buffer)));
    if (length > 0 && length < std::size(buffer))
    {
        return std::filesystem::path(buffer);
    }

    return std::filesystem::path(L"C:\\Program Files");
}

std::filesystem::path FindLatestPixRuntimePath()
{
    const std::filesystem::path pixRoot = GetProgramFilesDirectory() / L"Microsoft PIX";
    std::vector<std::filesystem::path> versionDirectories;

    std::error_code error;
    if (!std::filesystem::exists(pixRoot, error))
    {
        return {};
    }

    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(pixRoot, error))
    {
        if (error)
        {
            return {};
        }

        if (!entry.is_directory())
        {
            continue;
        }

        const std::filesystem::path directoryPath = entry.path();
        if (!IsVersionDirectory(directoryPath))
        {
            continue;
        }

        const std::filesystem::path candidate = directoryPath / L"WinPixEventRuntime_OneCore.dll";
        if (std::filesystem::exists(candidate, error))
        {
            versionDirectories.push_back(directoryPath);
        }
    }

    if (versionDirectories.empty())
    {
        return {};
    }

    std::sort(versionDirectories.begin(), versionDirectories.end(), CompareVersionDirectoryNames);
    return versionDirectories.back() / L"WinPixEventRuntime_OneCore.dll";
}

} // namespace

void PixProgrammaticCapture::Initialize()
{
    if (IsAvailable())
    {
        return;
    }

    HMODULE existingModule = ::GetModuleHandleW(L"WinPixEventRuntime_OneCore.dll");
    if (!existingModule)
    {
        const std::filesystem::path runtimePath = FindLatestPixRuntimePath();
        if (runtimePath.empty())
        {
            WEST_LOG_WARNING(LogCategory::Core, "PIX programmatic capture was requested, but no PIX runtime was found.");
            return;
        }

        existingModule = ::LoadLibraryW(runtimePath.c_str());
        if (!existingModule)
        {
            WEST_LOG_WARNING(LogCategory::Core, "Failed to load PIX runtime from '{}'.", runtimePath.string());
            return;
        }
    }

    m_runtimeModule = existingModule;
    m_beginCapture = reinterpret_cast<BeginCaptureFn>(::GetProcAddress(static_cast<HMODULE>(m_runtimeModule), "PIXBeginCapture2"));
    m_endCapture = reinterpret_cast<EndCaptureFn>(::GetProcAddress(static_cast<HMODULE>(m_runtimeModule), "PIXEndCapture"));

    if (!IsAvailable())
    {
        WEST_LOG_WARNING(LogCategory::Core, "PIX runtime is loaded, but programmatic capture exports are unavailable.");
        m_beginCapture = nullptr;
        m_endCapture = nullptr;
        return;
    }

    WEST_LOG_INFO(LogCategory::Core, "PIX programmatic capture API detected.");
}

bool PixProgrammaticCapture::BeginGpuCapture(const std::wstring& capturePath)
{
    if (!IsAvailable())
    {
        return false;
    }

    PIXCaptureParameters params{};
    params.GpuCaptureParameters.FileName = capturePath.c_str();

    const long hr = m_beginCapture(kPixCaptureGpu, &params);
    if (FAILED(hr))
    {
        WEST_LOG_WARNING(LogCategory::Core, "PIXBeginCapture2 failed with HRESULT 0x{:08X}.", static_cast<unsigned long>(hr));
        return false;
    }

    WEST_LOG_INFO(LogCategory::Core, "PIX GPU capture started: '{}'.", std::filesystem::path(capturePath).string());
    return true;
}

bool PixProgrammaticCapture::EndCapture()
{
    if (!IsAvailable())
    {
        return false;
    }

    const long hr = m_endCapture(FALSE);
    if (FAILED(hr))
    {
        WEST_LOG_WARNING(LogCategory::Core, "PIXEndCapture failed with HRESULT 0x{:08X}.", static_cast<unsigned long>(hr));
        return false;
    }

    WEST_LOG_INFO(LogCategory::Core, "PIX GPU capture ended.");
    return true;
}

} // namespace west
