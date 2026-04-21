// =============================================================================
// WestEngine - Shader
// Runtime shader bytecode loading for offline Slang outputs
// =============================================================================
#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace west::shader
{

class ShaderCompiler final
{
public:
    [[nodiscard]] static std::filesystem::path GetShaderOutputDirectory();
    [[nodiscard]] static bool LoadBytecode(std::string_view relativePath, std::vector<uint8_t>& outBytecode);
};

} // namespace west::shader
