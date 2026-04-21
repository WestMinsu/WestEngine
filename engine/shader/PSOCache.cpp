// =============================================================================
// WestEngine - Shader
// Pipeline state cache and stable PSO hashing
// =============================================================================
#include "shader/PSOCache.h"

#include "core/Logger.h"
#include "rhi/interface/IRHIDevice.h"
#include "rhi/interface/IRHIPipeline.h"

#include <string_view>
#include <type_traits>
#include <vector>

namespace west::shader
{
namespace
{

constexpr uint64_t kFnvOffsetBasis = 14695981039346656037ull;
constexpr uint64_t kFnvPrime = 1099511628211ull;

void HashRaw(uint64_t& hash, const void* data, size_t size)
{
    const auto* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; ++i)
    {
        hash ^= bytes[i];
        hash *= kFnvPrime;
    }
}

template <typename T>
void HashValue(uint64_t& hash, const T& value)
{
    HashRaw(hash, &value, sizeof(T));
}

void AppendRaw(std::vector<uint8_t>& keyBytes, const void* data, size_t size)
{
    if (size == 0)
    {
        return;
    }

    const auto* bytes = static_cast<const uint8_t*>(data);
    keyBytes.insert(keyBytes.end(), bytes, bytes + size);
}

template <typename T>
void AppendValue(std::vector<uint8_t>& keyBytes, const T& value)
{
    static_assert(std::is_trivially_copyable_v<T>);
    AppendRaw(keyBytes, &value, sizeof(T));
}

void AppendBool(std::vector<uint8_t>& keyBytes, bool value)
{
    const uint8_t serialized = value ? 1 : 0;
    AppendValue(keyBytes, serialized);
}

void AppendString(std::vector<uint8_t>& keyBytes, const char* text)
{
    const std::string_view view(text ? text : "");
    const uint64_t size = static_cast<uint64_t>(view.size());
    AppendValue(keyBytes, size);
    AppendRaw(keyBytes, view.data(), view.size());
}

void AppendByteSpan(std::vector<uint8_t>& keyBytes, std::span<const uint8_t> bytes)
{
    const uint64_t size = static_cast<uint64_t>(bytes.size());
    AppendValue(keyBytes, size);
    if (!bytes.empty())
    {
        AppendRaw(keyBytes, bytes.data(), bytes.size());
    }
}

template <typename T>
void AppendValueSpan(std::vector<uint8_t>& keyBytes, std::span<const T> values)
{
    const uint64_t size = static_cast<uint64_t>(values.size());
    AppendValue(keyBytes, size);
    for (const T& value : values)
    {
        AppendValue(keyBytes, value);
    }
}

void AppendBlendAttachment(std::vector<uint8_t>& keyBytes, const rhi::RHIBlendAttachment& attachment)
{
    AppendBool(keyBytes, attachment.blendEnable);
    AppendValue(keyBytes, attachment.srcColor);
    AppendValue(keyBytes, attachment.dstColor);
    AppendValue(keyBytes, attachment.colorOp);
    AppendValue(keyBytes, attachment.srcAlpha);
    AppendValue(keyBytes, attachment.dstAlpha);
    AppendValue(keyBytes, attachment.alphaOp);
}

} // namespace

uint64_t PSOCache::HashBytes(std::span<const uint8_t> bytes)
{
    uint64_t hash = kFnvOffsetBasis;
    const uint64_t size = static_cast<uint64_t>(bytes.size());
    HashValue(hash, size);
    if (!bytes.empty())
    {
        HashRaw(hash, bytes.data(), bytes.size());
    }
    return hash;
}

std::vector<uint8_t> PSOCache::BuildGraphicsPipelineKey(const rhi::RHIGraphicsPipelineDesc& desc)
{
    std::vector<uint8_t> keyBytes;

    const uint8_t typeTag = 1;
    AppendValue(keyBytes, typeTag);
    AppendByteSpan(keyBytes, desc.vertexShader);
    AppendByteSpan(keyBytes, desc.fragmentShader);

    const uint64_t vertexAttributeCount = static_cast<uint64_t>(desc.vertexAttributes.size());
    AppendValue(keyBytes, vertexAttributeCount);
    for (const rhi::RHIVertexAttribute& attribute : desc.vertexAttributes)
    {
        AppendString(keyBytes, attribute.semantic);
        AppendValue(keyBytes, attribute.format);
        AppendValue(keyBytes, attribute.offset);
    }

    AppendValue(keyBytes, desc.vertexStride);
    AppendValue(keyBytes, desc.topology);
    AppendValue(keyBytes, desc.cullMode);
    AppendValue(keyBytes, desc.fillMode);
    AppendValue(keyBytes, desc.depthCompare);
    AppendBool(keyBytes, desc.depthWrite);
    AppendBool(keyBytes, desc.depthTest);
    AppendValueSpan(keyBytes, desc.colorFormats);
    AppendValue(keyBytes, desc.depthFormat);

    const uint64_t blendAttachmentCount = static_cast<uint64_t>(desc.blendAttachments.size());
    AppendValue(keyBytes, blendAttachmentCount);
    for (const rhi::RHIBlendAttachment& attachment : desc.blendAttachments)
    {
        AppendBlendAttachment(keyBytes, attachment);
    }

    AppendValue(keyBytes, desc.pushConstantSizeBytes);
    return keyBytes;
}

std::vector<uint8_t> PSOCache::BuildComputePipelineKey(const rhi::RHIComputePipelineDesc& desc)
{
    std::vector<uint8_t> keyBytes;

    const uint8_t typeTag = 2;
    AppendValue(keyBytes, typeTag);
    AppendByteSpan(keyBytes, desc.computeShader);
    AppendValue(keyBytes, desc.pushConstantSizeBytes);
    return keyBytes;
}

uint64_t PSOCache::ComputeGraphicsPipelineHash(const rhi::RHIGraphicsPipelineDesc& desc)
{
    const std::vector<uint8_t> keyBytes = BuildGraphicsPipelineKey(desc);
    return HashBytes(keyBytes);
}

uint64_t PSOCache::ComputeComputePipelineHash(const rhi::RHIComputePipelineDesc& desc)
{
    const std::vector<uint8_t> keyBytes = BuildComputePipelineKey(desc);
    return HashBytes(keyBytes);
}

rhi::IRHIPipeline* PSOCache::GetOrCreateGraphicsPipeline(
    rhi::IRHIDevice& device,
    const rhi::RHIGraphicsPipelineDesc& desc)
{
    std::vector<uint8_t> keyBytes = BuildGraphicsPipelineKey(desc);
    const uint64_t hash = HashBytes(keyBytes);

    std::lock_guard lock(m_mutex);
    auto& bucket = m_cache[hash];
    for (const CacheEntry& entry : bucket)
    {
        if (entry.keyBytes == keyBytes)
        {
            WEST_LOG_INFO(LogCategory::Shader, "PSO cache hit: graphics 0x{:016X}", hash);
            return entry.pipeline.get();
        }
    }

    WEST_LOG_INFO(LogCategory::Shader, "PSO cache miss: graphics 0x{:016X}", hash);

    rhi::RHIGraphicsPipelineDesc cacheDesc = desc;
    cacheDesc.psoHash = hash;
    auto pipeline = device.CreateGraphicsPipeline(cacheDesc);
    if (!pipeline)
    {
        WEST_LOG_ERROR(LogCategory::Shader, "Failed to create graphics PSO 0x{:016X}", hash);
        return nullptr;
    }

    auto* result = pipeline.get();
    bucket.push_back(CacheEntry{std::move(keyBytes), std::move(pipeline)});
    return result;
}

rhi::IRHIPipeline* PSOCache::GetOrCreateComputePipeline(
    rhi::IRHIDevice& device,
    const rhi::RHIComputePipelineDesc& desc)
{
    std::vector<uint8_t> keyBytes = BuildComputePipelineKey(desc);
    const uint64_t hash = HashBytes(keyBytes);

    std::lock_guard lock(m_mutex);
    auto& bucket = m_cache[hash];
    for (const CacheEntry& entry : bucket)
    {
        if (entry.keyBytes == keyBytes)
        {
            WEST_LOG_INFO(LogCategory::Shader, "PSO cache hit: compute 0x{:016X}", hash);
            return entry.pipeline.get();
        }
    }

    WEST_LOG_INFO(LogCategory::Shader, "PSO cache miss: compute 0x{:016X}", hash);

    rhi::RHIComputePipelineDesc cacheDesc = desc;
    cacheDesc.psoHash = hash;
    auto pipeline = device.CreateComputePipeline(cacheDesc);
    if (!pipeline)
    {
        WEST_LOG_ERROR(LogCategory::Shader, "Failed to create compute PSO 0x{:016X}", hash);
        return nullptr;
    }

    auto* result = pipeline.get();
    bucket.push_back(CacheEntry{std::move(keyBytes), std::move(pipeline)});
    return result;
}

} // namespace west::shader
