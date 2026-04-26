// =============================================================================
// WestEngine - Shader
// Runtime shader bytecode loading for offline Slang outputs
// =============================================================================
#pragma once

#include "rhi/interface/RHIEnums.h"

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace west::shader
{

class ShaderCompiler final
{
public:
    enum class Stage
    {
        Vertex,
        Fragment,
        Compute
    };

    [[nodiscard]] static std::filesystem::path GetShaderOutputDirectory();
    [[nodiscard]] static bool LoadBytecode(std::string_view relativePath, std::vector<uint8_t>& outBytecode);
    [[nodiscard]] static bool LoadStageBytecode(rhi::RHIBackend backend, std::string_view shaderName,
                                                Stage stage, std::vector<uint8_t>& outBytecode);
};

} // namespace west::shader
