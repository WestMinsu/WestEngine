// =============================================================================
// WestEngine - Platform (Win32)
// Optional PIX GPU capturer loader for DX12 capture sessions
// =============================================================================
#include "platform/win32/PixGpuCapturerLoader.h"

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

std::filesystem::path FindLatestPixGpuCapturerPath()
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

        const std::filesystem::path candidate = directoryPath / L"WinPixGpuCapturer.dll";
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
    return versionDirectories.back() / L"WinPixGpuCapturer.dll";
}

} // namespace

void PixGpuCapturerLoader::Initialize()
{
    if (m_module)
    {
        return;
    }

    HMODULE existingModule = ::GetModuleHandleW(L"WinPixGpuCapturer.dll");
    if (existingModule)
    {
        m_module = existingModule;
        WEST_LOG_INFO(LogCategory::Core, "PIX GPU capturer already loaded.");
        return;
    }

    const std::filesystem::path dllPath = FindLatestPixGpuCapturerPath();
    if (dllPath.empty())
    {
        WEST_LOG_WARNING(LogCategory::Core, "PIX GPU capturer was requested, but no PIX installation was found.");
        return;
    }

    HMODULE module = ::LoadLibraryW(dllPath.c_str());
    if (!module)
    {
        WEST_LOG_WARNING(LogCategory::Core, "Failed to load PIX GPU capturer from '{}'.", dllPath.string());
        return;
    }

    m_module = module;
    WEST_LOG_INFO(LogCategory::Core, "PIX GPU capturer loaded from '{}'.", dllPath.string());
}

} // namespace west
