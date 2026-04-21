// =============================================================================
// WestEngine - Shader
// Runtime shader bytecode loading for offline Slang outputs
// =============================================================================
#include "shader/ShaderCompiler.h"

#include "core/Logger.h"

#include <fstream>

#ifndef WEST_SHADER_OUTPUT_DIR
#define WEST_SHADER_OUTPUT_DIR "build/shaders"
#endif

namespace west::shader
{

std::filesystem::path ShaderCompiler::GetShaderOutputDirectory()
{
    return std::filesystem::path(WEST_SHADER_OUTPUT_DIR);
}

bool ShaderCompiler::LoadBytecode(std::string_view relativePath, std::vector<uint8_t>& outBytecode)
{
    outBytecode.clear();

    const std::filesystem::path shaderPath = GetShaderOutputDirectory() / std::filesystem::path(relativePath);

    std::error_code fileSizeError;
    const uint64_t fileSize = std::filesystem::file_size(shaderPath, fileSizeError);
    if (fileSizeError || fileSize == 0)
    {
        WEST_LOG_ERROR(LogCategory::Shader, "Failed to stat shader bytecode '{}'", shaderPath.string());
        return false;
    }

    std::ifstream file(shaderPath, std::ios::binary);
    if (!file)
    {
        WEST_LOG_ERROR(LogCategory::Shader, "Failed to open shader bytecode '{}'", shaderPath.string());
        return false;
    }

    outBytecode.resize(static_cast<size_t>(fileSize));
    file.read(reinterpret_cast<char*>(outBytecode.data()), static_cast<std::streamsize>(outBytecode.size()));
    if (!file)
    {
        WEST_LOG_ERROR(LogCategory::Shader, "Failed to read shader bytecode '{}'", shaderPath.string());
        outBytecode.clear();
        return false;
    }

    WEST_LOG_INFO(LogCategory::Shader, "Loaded shader bytecode '{}' ({} bytes)", shaderPath.string(), fileSize);
    return true;
}

} // namespace west::shader
