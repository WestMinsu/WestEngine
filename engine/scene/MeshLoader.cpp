// =============================================================================
// WestEngine - Scene
// Canonical glTF static-scene loader for Slice C
// =============================================================================
#include "scene/MeshLoader.h"

#include "core/Assert.h"
#include "core/Logger.h"
#include "scene/SceneAssetLoader.h"

#define CGLTF_IMPLEMENTATION
#include <algorithm>
#include <array>
#include <bit>
#include <cfloat>
#include <cgltf.h>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace west::scene
{

namespace
{

using Clock = std::chrono::steady_clock;

struct GeometryCounters
{
    uint32_t meshCount = 0;
    uint32_t instanceCount = 0;
    uint64_t vertexCount = 0;
    uint64_t indexCount = 0;
};

struct MergeKey
{
    uint32_t materialIndex = 0;
    std::array<uint32_t, 16> matrixBits = {};

    [[nodiscard]] bool operator==(const MergeKey& rhs) const
    {
        return materialIndex == rhs.materialIndex && matrixBits == rhs.matrixBits;
    }
};

struct MergeKeyHasher
{
    [[nodiscard]] size_t operator()(const MergeKey& key) const noexcept
    {
        size_t hash = std::hash<uint32_t>{}(key.materialIndex);
        for (uint32_t value : key.matrixBits)
        {
            hash ^= std::hash<uint32_t>{}(value) + 0x9E3779B9u + (hash << 6u) + (hash >> 2u);
        }
        return hash;
    }
};

[[nodiscard]] const char* CgltfResultName(cgltf_result result)
{
    switch (result)
    {
    case cgltf_result_success:
        return "success";
    case cgltf_result_data_too_short:
        return "data_too_short";
    case cgltf_result_unknown_format:
        return "unknown_format";
    case cgltf_result_invalid_json:
        return "invalid_json";
    case cgltf_result_invalid_gltf:
        return "invalid_gltf";
    case cgltf_result_invalid_options:
        return "invalid_options";
    case cgltf_result_file_not_found:
        return "file_not_found";
    case cgltf_result_io_error:
        return "io_error";
    case cgltf_result_out_of_memory:
        return "out_of_memory";
    case cgltf_result_legacy_gltf:
        return "legacy_gltf";
    default:
        return "unknown";
    }
}

[[nodiscard]] std::filesystem::path NormalizePath(const std::filesystem::path& path)
{
    std::error_code errorCode;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, errorCode);
    if (!errorCode)
    {
        return canonical;
    }

    return path.lexically_normal();
}

[[nodiscard]] std::array<float, 16> IdentityMatrix()
{
    return {
        1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] std::array<float, 16> UniformScale(float scale)
{
    return {
        scale, 0.0f, 0.0f, 0.0f, 0.0f, scale, 0.0f, 0.0f, 0.0f, 0.0f, scale, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f,
    };
}

[[nodiscard]] std::array<float, 16> Multiply(const std::array<float, 16>& lhs, const std::array<float, 16>& rhs)
{
    std::array<float, 16> result{};
    for (int row = 0; row < 4; ++row)
    {
        for (int column = 0; column < 4; ++column)
        {
            float sum = 0.0f;
            for (int k = 0; k < 4; ++k)
            {
                sum += lhs[row * 4 + k] * rhs[k * 4 + column];
            }
            result[row * 4 + column] = sum;
        }
    }
    return result;
}

[[nodiscard]] std::array<float, 3> TransformPoint(const std::array<float, 16>& matrix, const float position[3])
{
    return {
        (position[0] * matrix[0]) + (position[1] * matrix[4]) + (position[2] * matrix[8]) + matrix[12],
        (position[0] * matrix[1]) + (position[1] * matrix[5]) + (position[2] * matrix[9]) + matrix[13],
        (position[0] * matrix[2]) + (position[1] * matrix[6]) + (position[2] * matrix[10]) + matrix[14],
    };
}

[[nodiscard]] float Clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] GeometryCounters CountGeometry(const SceneAsset& asset)
{
    GeometryCounters counters{};
    counters.meshCount = static_cast<uint32_t>(asset.GetMeshes().size());
    counters.instanceCount = static_cast<uint32_t>(asset.GetInstances().size());

    for (const MeshData& mesh : asset.GetMeshes())
    {
        counters.vertexCount += static_cast<uint64_t>(mesh.vertices.size());
        counters.indexCount += static_cast<uint64_t>(mesh.indices.size());
    }

    return counters;
}

[[nodiscard]] MergeKey MakeMergeKey(uint32_t materialIndex, const std::array<float, 16>& modelMatrix)
{
    MergeKey key{};
    key.materialIndex = materialIndex;
    for (size_t i = 0; i < modelMatrix.size(); ++i)
    {
        key.matrixBits[i] = std::bit_cast<uint32_t>(modelMatrix[i]);
    }
    return key;
}

[[nodiscard]] uint32_t MergeStaticMeshesByMaterialAndTransform(SceneAsset& asset)
{
    const auto& sourceMeshes = asset.GetMeshes();
    const auto& sourceInstances = asset.GetInstances();
    if (sourceMeshes.empty() || sourceInstances.size() < 2)
    {
        return 0;
    }

    std::vector<MeshData> mergedMeshes;
    std::vector<InstanceData> mergedInstances;
    mergedMeshes.reserve(sourceInstances.size());
    mergedInstances.reserve(sourceInstances.size());

    std::unordered_map<MergeKey, uint32_t, MergeKeyHasher> mergedLookup;
    uint32_t mergedInstanceCount = 0;

    for (const InstanceData& instance : sourceInstances)
    {
        WEST_ASSERT(instance.meshIndex < sourceMeshes.size());
        const MeshData& sourceMesh = sourceMeshes[instance.meshIndex];
        if (sourceMesh.vertices.empty() || sourceMesh.indices.empty())
        {
            continue;
        }

        const MergeKey key = MakeMergeKey(sourceMesh.materialIndex, instance.modelMatrix);
        auto [it, inserted] = mergedLookup.try_emplace(key, static_cast<uint32_t>(mergedMeshes.size()));
        if (inserted)
        {
            MeshData mergedMesh{};
            mergedMesh.materialIndex = sourceMesh.materialIndex;
            mergedMesh.debugName = sourceMesh.debugName;
            mergedMeshes.push_back(std::move(mergedMesh));

            InstanceData mergedInstance{};
            mergedInstance.meshIndex = it->second;
            mergedInstance.modelMatrix = instance.modelMatrix;
            mergedInstances.push_back(std::move(mergedInstance));
        }
        else
        {
            ++mergedInstanceCount;
        }

        MeshData& mergedMesh = mergedMeshes[it->second];
        const uint32_t baseVertex = static_cast<uint32_t>(mergedMesh.vertices.size());
        mergedMesh.vertices.insert(mergedMesh.vertices.end(), sourceMesh.vertices.begin(), sourceMesh.vertices.end());
        mergedMesh.indices.reserve(mergedMesh.indices.size() + sourceMesh.indices.size());
        for (uint32_t index : sourceMesh.indices)
        {
            mergedMesh.indices.push_back(index + baseVertex);
        }
    }

    if (mergedInstanceCount == 0)
    {
        return 0;
    }

    for (uint32_t meshIndex = 0; meshIndex < mergedInstances.size(); ++meshIndex)
    {
        mergedInstances[meshIndex].meshIndex = meshIndex;
    }

    asset.ReplaceGeometry(std::move(mergedMeshes), std::move(mergedInstances));
    return mergedInstanceCount;
}

void ComputeBounds(SceneAsset& asset)
{
    std::array<float, 3> boundsMin = {FLT_MAX, FLT_MAX, FLT_MAX};
    std::array<float, 3> boundsMax = {-FLT_MAX, -FLT_MAX, -FLT_MAX};

    for (const InstanceData& instance : asset.GetInstances())
    {
        WEST_ASSERT(instance.meshIndex < asset.GetMeshes().size());
        const MeshData& mesh = asset.GetMeshes()[instance.meshIndex];
        for (const MeshVertex& vertex : mesh.vertices)
        {
            const std::array<float, 3> transformed = TransformPoint(instance.modelMatrix, vertex.position);
            boundsMin[0] = std::min(boundsMin[0], transformed[0]);
            boundsMin[1] = std::min(boundsMin[1], transformed[1]);
            boundsMin[2] = std::min(boundsMin[2], transformed[2]);
            boundsMax[0] = std::max(boundsMax[0], transformed[0]);
            boundsMax[1] = std::max(boundsMax[1], transformed[1]);
            boundsMax[2] = std::max(boundsMax[2], transformed[2]);
        }
    }

    if (asset.GetInstances().empty())
    {
        boundsMin = {0.0f, 0.0f, 0.0f};
        boundsMax = {0.0f, 0.0f, 0.0f};
    }

    asset.SetBounds(boundsMin, boundsMax);
}

[[nodiscard]] uint32_t ResolveTextureIndex(const std::filesystem::path& sceneSourceFile,
                                           const cgltf_texture_view& textureView,
                                           std::unordered_map<std::string, uint32_t>& textureIndices, SceneAsset& asset)
{
    if (textureView.texture == nullptr || textureView.texture->image == nullptr)
    {
        return kInvalidSceneTextureIndex;
    }

    const cgltf_image& image = *textureView.texture->image;
    if (image.uri == nullptr || image.uri[0] == '\0')
    {
        Logger::Log(
            LogLevel::Warning, LogCategory::Scene,
            std::format("Skipping embedded glTF image on {} because the runtime texture path expects file URIs.",
                        sceneSourceFile.string()));
        return kInvalidSceneTextureIndex;
    }

    const std::string_view imageUri(image.uri);
    if (imageUri.starts_with("data:"))
    {
        Logger::Log(
            LogLevel::Warning, LogCategory::Scene,
            std::format("Skipping data-URI glTF image on {} because the runtime texture path expects file URIs.",
                        sceneSourceFile.string()));
        return kInvalidSceneTextureIndex;
    }

    const std::filesystem::path resolvedPath = NormalizePath(sceneSourceFile.parent_path() / image.uri);
    const std::string textureKey = resolvedPath.generic_string();
    if (const auto existing = textureIndices.find(textureKey); existing != textureIndices.end())
    {
        return existing->second;
    }

    TextureAsset texture{};
    texture.sourcePath = resolvedPath;
    texture.debugName = !std::string_view(textureView.texture->name ? textureView.texture->name : "").empty()
                            ? textureView.texture->name
                            : resolvedPath.filename().string();

    const uint32_t textureIndex = asset.AddTexture(std::move(texture));
    textureIndices.emplace(textureKey, textureIndex);
    return textureIndex;
}

[[nodiscard]] Material ConvertMaterial(const std::filesystem::path& sceneSourceFile, const cgltf_material& material,
                                       std::unordered_map<std::string, uint32_t>& textureIndices, SceneAsset& asset)
{
    Material converted{};
    if (material.name != nullptr)
    {
        converted.debugName = material.name;
    }
    else
    {
        converted.debugName = "GltfMaterial";
    }

    if (material.has_pbr_metallic_roughness)
    {
        const cgltf_pbr_metallic_roughness& pbr = material.pbr_metallic_roughness;
        converted.baseColor = {
            pbr.base_color_factor[0],
            pbr.base_color_factor[1],
            pbr.base_color_factor[2],
            pbr.base_color_factor[3],
        };
        converted.roughness = Clamp01(pbr.roughness_factor);
        converted.metallic = Clamp01(pbr.metallic_factor);
        converted.baseColorTextureIndex =
            ResolveTextureIndex(sceneSourceFile, pbr.base_color_texture, textureIndices, asset);
    }

    if (material.alpha_mode == cgltf_alpha_mode_mask)
    {
        converted.alphaCutoff = Clamp01(material.alpha_cutoff);
    }

    return converted;
}

[[nodiscard]] const cgltf_accessor* RequireAccessor(const cgltf_primitive& primitive, cgltf_attribute_type type,
                                                    cgltf_int attributeIndex)
{
    const cgltf_accessor* accessor = cgltf_find_accessor(&primitive, type, attributeIndex);
    WEST_CHECK(accessor != nullptr, "glTF primitive is missing a required accessor");
    WEST_CHECK(accessor->count > 0, "glTF accessor is empty");
    if (type == cgltf_attribute_type_position)
    {
        WEST_CHECK(accessor->type == cgltf_type_vec3 && accessor->component_type == cgltf_component_type_r_32f,
                   "glTF POSITION accessor must be float3");
    }
    return accessor;
}

[[nodiscard]] MeshData ConvertPrimitive(const cgltf_primitive& primitive, std::string_view meshName,
                                        uint32_t primitiveIndex, uint32_t materialIndex)
{
    WEST_CHECK(primitive.type == cgltf_primitive_type_triangles, "Only triangle-list glTF primitives are supported");

    const cgltf_accessor* positionAccessor = RequireAccessor(primitive, cgltf_attribute_type_position, 0);
    const cgltf_accessor* normalAccessor = cgltf_find_accessor(&primitive, cgltf_attribute_type_normal, 0);
    const cgltf_accessor* uvAccessor = cgltf_find_accessor(&primitive, cgltf_attribute_type_texcoord, 0);

    const uint32_t vertexCount = static_cast<uint32_t>(positionAccessor->count);
    MeshData converted{};
    converted.materialIndex = materialIndex;
    converted.debugName = meshName.empty() ? std::format("GltfMesh_{}", primitiveIndex)
                                           : std::format("{}_primitive{}", meshName, primitiveIndex);
    converted.vertices.resize(vertexCount);

    for (uint32_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
    {
        MeshVertex& vertex = converted.vertices[vertexIndex];

        float position[3] = {};
        WEST_CHECK(cgltf_accessor_read_float(positionAccessor, vertexIndex, position, 3) != 0,
                   "Failed to read glTF POSITION accessor");
        vertex.position[0] = position[0];
        vertex.position[1] = position[1];
        vertex.position[2] = position[2];

        if (normalAccessor != nullptr)
        {
            float normal[3] = {};
            WEST_CHECK(cgltf_accessor_read_float(normalAccessor, vertexIndex, normal, 3) != 0,
                       "Failed to read glTF NORMAL accessor");
            vertex.normal[0] = normal[0];
            vertex.normal[1] = normal[1];
            vertex.normal[2] = normal[2];
        }
        else
        {
            vertex.normal[0] = 0.0f;
            vertex.normal[1] = 1.0f;
            vertex.normal[2] = 0.0f;
        }

        if (uvAccessor != nullptr)
        {
            float uv[2] = {};
            WEST_CHECK(cgltf_accessor_read_float(uvAccessor, vertexIndex, uv, 2) != 0,
                       "Failed to read glTF TEXCOORD_0 accessor");
            vertex.uv[0] = uv[0];
            vertex.uv[1] = 1.0f - uv[1];
        }
        else
        {
            vertex.uv[0] = 0.0f;
            vertex.uv[1] = 0.0f;
        }
    }

    if (primitive.indices != nullptr)
    {
        converted.indices.reserve(static_cast<size_t>(primitive.indices->count));
        for (cgltf_size index = 0; index < primitive.indices->count; ++index)
        {
            converted.indices.push_back(static_cast<uint32_t>(cgltf_accessor_read_index(primitive.indices, index)));
        }
    }
    else
    {
        WEST_CHECK((vertexCount % 3u) == 0, "Non-indexed glTF triangle primitive has an invalid vertex count");
        converted.indices.resize(vertexCount);
        for (uint32_t index = 0; index < vertexCount; ++index)
        {
            converted.indices[index] = index;
        }
    }

    WEST_CHECK(!converted.indices.empty(), "glTF primitive produced no indices");
    return converted;
}

[[nodiscard]] uint32_t EnsureDefaultMaterial(SceneAsset& asset, std::optional<uint32_t>& defaultMaterialIndex)
{
    if (!defaultMaterialIndex.has_value())
    {
        Material defaultMaterial{};
        defaultMaterial.debugName = "DefaultGltfMaterial";
        defaultMaterialIndex = asset.AddMaterial(std::move(defaultMaterial));
    }

    return *defaultMaterialIndex;
}

void AddNodeInstances(const cgltf_data& data, const cgltf_node& node,
                      const std::vector<std::vector<uint32_t>>& primitiveMeshIndices,
                      const std::array<float, 16>& sceneScale, SceneAsset& asset)
{
    float worldTransformRaw[16] = {};
    cgltf_node_transform_world(&node, worldTransformRaw);

    std::array<float, 16> worldTransform{};
    for (size_t i = 0; i < worldTransform.size(); ++i)
    {
        worldTransform[i] = worldTransformRaw[i];
    }
    const std::array<float, 16> modelMatrix = Multiply(worldTransform, sceneScale);

    if (node.mesh != nullptr)
    {
        const cgltf_size meshIndex = cgltf_mesh_index(&data, node.mesh);
        WEST_ASSERT(meshIndex < primitiveMeshIndices.size());
        for (uint32_t primitiveMeshIndex : primitiveMeshIndices[meshIndex])
        {
            InstanceData instance{};
            instance.meshIndex = primitiveMeshIndex;
            instance.modelMatrix = modelMatrix;
            asset.AddInstance(instance);
        }
    }

    for (cgltf_size childIndex = 0; childIndex < node.children_count; ++childIndex)
    {
        WEST_ASSERT(node.children[childIndex] != nullptr);
        AddNodeInstances(data, *node.children[childIndex], primitiveMeshIndices, sceneScale, asset);
    }
}

void AddSceneInstances(const cgltf_data& data, const std::vector<std::vector<uint32_t>>& primitiveMeshIndices,
                       float uniformScale, SceneAsset& asset)
{
    const std::array<float, 16> sceneScale = UniformScale(uniformScale);
    const cgltf_scene* activeScene =
        data.scene != nullptr ? data.scene : (data.scenes_count > 0 ? &data.scenes[0] : nullptr);

    if (activeScene != nullptr && activeScene->nodes_count > 0)
    {
        for (cgltf_size nodeIndex = 0; nodeIndex < activeScene->nodes_count; ++nodeIndex)
        {
            WEST_ASSERT(activeScene->nodes[nodeIndex] != nullptr);
            AddNodeInstances(data, *activeScene->nodes[nodeIndex], primitiveMeshIndices, sceneScale, asset);
        }
        return;
    }

    for (cgltf_size nodeIndex = 0; nodeIndex < data.nodes_count; ++nodeIndex)
    {
        const cgltf_node& node = data.nodes[nodeIndex];
        if (node.parent == nullptr)
        {
            AddNodeInstances(data, node, primitiveMeshIndices, sceneScale, asset);
        }
    }
}

} // namespace

SceneAsset MeshLoader::LoadStaticSceneGltf(const std::filesystem::path& sourceFile, const SceneLoadOptions& options)
{
    const auto totalStart = Clock::now();
    const std::filesystem::path normalizedSource = NormalizePath(sourceFile);
    if (!std::filesystem::exists(normalizedSource))
    {
        WEST_LOG_FATAL(LogCategory::Scene, "glTF scene file not found: {}", normalizedSource.string());
        WEST_CHECK(false, "glTF scene source file is missing");
    }

    Logger::Log(LogLevel::Info, LogCategory::Scene,
                std::format("Loading canonical glTF scene {} (merge={}, scale={:.4f}).", normalizedSource.string(),
                            options.enableStaticMeshMerge, options.uniformScale));

    cgltf_options cgltfOptions{};
    cgltf_data* parsedData = nullptr;

    const cgltf_result parseResult = cgltf_parse_file(&cgltfOptions, normalizedSource.string().c_str(), &parsedData);
    WEST_CHECK(parseResult == cgltf_result_success && parsedData != nullptr, "Failed to parse glTF scene");

    struct CgltfDataDeleter
    {
        void operator()(cgltf_data* data) const
        {
            if (data != nullptr)
            {
                cgltf_free(data);
            }
        }
    };

    const std::unique_ptr<cgltf_data, CgltfDataDeleter> data(parsedData);

    const cgltf_result loadBufferResult =
        cgltf_load_buffers(&cgltfOptions, data.get(), normalizedSource.string().c_str());
    WEST_CHECK(loadBufferResult == cgltf_result_success, "Failed to load glTF buffers");

    const cgltf_result validateResult = cgltf_validate(data.get());
    WEST_CHECK(validateResult == cgltf_result_success, "glTF validation failed");

    SceneAsset asset{};
    asset.SetDebugName(normalizedSource.stem().string());

    SceneLoadStats loadStats{};
    const auto importStart = Clock::now();

    std::unordered_map<std::string, uint32_t> textureIndices;
    std::optional<uint32_t> defaultMaterialIndex;
    if (data->materials_count == 0)
    {
        [[maybe_unused]] const uint32_t materialIndex = EnsureDefaultMaterial(asset, defaultMaterialIndex);
        WEST_ASSERT(materialIndex == 0);
    }
    else
    {
        for (cgltf_size materialIndex = 0; materialIndex < data->materials_count; ++materialIndex)
        {
            asset.AddMaterial(ConvertMaterial(normalizedSource, data->materials[materialIndex], textureIndices, asset));
        }
    }

    std::vector<std::vector<uint32_t>> primitiveMeshIndices(data->meshes_count);
    for (cgltf_size meshIndex = 0; meshIndex < data->meshes_count; ++meshIndex)
    {
        const cgltf_mesh& mesh = data->meshes[meshIndex];
        primitiveMeshIndices[meshIndex].reserve(mesh.primitives_count);

        for (cgltf_size primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex)
        {
            const cgltf_primitive& primitive = mesh.primitives[primitiveIndex];
            const uint32_t materialIndex =
                primitive.material != nullptr
                    ? static_cast<uint32_t>(cgltf_material_index(data.get(), primitive.material))
                    : EnsureDefaultMaterial(asset, defaultMaterialIndex);

            MeshData convertedMesh = ConvertPrimitive(primitive, mesh.name != nullptr ? mesh.name : "",
                                                      static_cast<uint32_t>(primitiveIndex), materialIndex);
            primitiveMeshIndices[meshIndex].push_back(asset.AddMesh(std::move(convertedMesh)));
        }
    }

    WEST_CHECK(!asset.GetMeshes().empty(), "glTF scene did not produce any renderable static meshes");

    AddSceneInstances(*data, primitiveMeshIndices, options.uniformScale, asset);
    if (asset.GetInstances().empty())
    {
        const std::array<float, 16> modelMatrix = Multiply(IdentityMatrix(), UniformScale(options.uniformScale));
        for (uint32_t meshIndex = 0; meshIndex < asset.GetMeshes().size(); ++meshIndex)
        {
            InstanceData instance{};
            instance.meshIndex = meshIndex;
            instance.modelMatrix = modelMatrix;
            asset.AddInstance(instance);
        }
    }

    loadStats.importMs = std::chrono::duration<double, std::milli>(Clock::now() - importStart).count();

    const GeometryCounters sourceGeometry = CountGeometry(asset);
    loadStats.sourceMeshCount = sourceGeometry.meshCount;
    loadStats.sourceInstanceCount = sourceGeometry.instanceCount;
    loadStats.sourceVertexCount = sourceGeometry.vertexCount;
    loadStats.sourceIndexCount = sourceGeometry.indexCount;

    if (options.enableStaticMeshMerge)
    {
        const auto optimizeStart = Clock::now();
        const uint32_t mergedInstances = MergeStaticMeshesByMaterialAndTransform(asset);
        loadStats.optimizeMs = std::chrono::duration<double, std::milli>(Clock::now() - optimizeStart).count();
        loadStats.appliedMeshMerge = mergedInstances > 0;
    }

    const GeometryCounters optimizedGeometry = CountGeometry(asset);
    loadStats.optimizedMeshCount = optimizedGeometry.meshCount;
    loadStats.optimizedInstanceCount = optimizedGeometry.instanceCount;
    loadStats.optimizedVertexCount = optimizedGeometry.vertexCount;
    loadStats.optimizedIndexCount = optimizedGeometry.indexCount;

    ComputeBounds(asset);

    loadStats.totalLoadMs = std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
    asset.SetLoadStats(loadStats);

    Logger::Log(
        LogLevel::Info, LogCategory::Scene,
        std::format("Loaded canonical glTF scene {} (meshes {}->{}, instances {}->{}, materials={}, textures={}, "
                    "{:.2f} ms, parse={}, buffers={}, validate={}).",
                    normalizedSource.string(), loadStats.sourceMeshCount, loadStats.optimizedMeshCount,
                    loadStats.sourceInstanceCount, loadStats.optimizedInstanceCount, asset.GetMaterials().size(),
                    asset.GetTextures().size(), loadStats.totalLoadMs, CgltfResultName(parseResult),
                    CgltfResultName(loadBufferResult), CgltfResultName(validateResult)));

    return asset;
}

} // namespace west::scene
