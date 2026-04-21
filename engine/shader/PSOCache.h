// =============================================================================
// WestEngine - Shader
// Pipeline state cache and stable PSO hashing
// =============================================================================
#pragma once

#include "rhi/interface/RHIDescriptors.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <unordered_map>
#include <vector>

namespace west::rhi
{
class IRHIDevice;
class IRHIPipeline;
} // namespace west::rhi

namespace west::shader
{

class PSOCache final
{
public:
    [[nodiscard]] rhi::IRHIPipeline* GetOrCreateGraphicsPipeline(
        rhi::IRHIDevice& device,
        const rhi::RHIGraphicsPipelineDesc& desc);

    [[nodiscard]] rhi::IRHIPipeline* GetOrCreateComputePipeline(
        rhi::IRHIDevice& device,
        const rhi::RHIComputePipelineDesc& desc);

    [[nodiscard]] static uint64_t ComputeGraphicsPipelineHash(const rhi::RHIGraphicsPipelineDesc& desc);
    [[nodiscard]] static uint64_t ComputeComputePipelineHash(const rhi::RHIComputePipelineDesc& desc);
    [[nodiscard]] static uint64_t HashBytes(std::span<const uint8_t> bytes);

private:
    struct CacheEntry
    {
        std::vector<uint8_t> keyBytes;
        std::unique_ptr<rhi::IRHIPipeline> pipeline;
    };

    [[nodiscard]] static std::vector<uint8_t> BuildGraphicsPipelineKey(const rhi::RHIGraphicsPipelineDesc& desc);
    [[nodiscard]] static std::vector<uint8_t> BuildComputePipelineKey(const rhi::RHIComputePipelineDesc& desc);

    std::mutex m_mutex;
    std::unordered_map<uint64_t, std::vector<CacheEntry>> m_cache;
};

} // namespace west::shader
