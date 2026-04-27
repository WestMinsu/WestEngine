// =============================================================================
// WestEngine - Scene
// Temporary static-scene import path for Bistro bring-up
// =============================================================================
#include "scene/SceneAssetLoader.h"

#include "core/Assert.h"
#include "core/Logger.h"
#include "scene/MeshLoader.h"
#include "scene/SceneMath.h"

#include <algorithm>
#include <array>
#include <assimp/GltfMaterial.h>
#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <bit>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <format>
#include <fstream>
#include <string_view>
#include <system_error>
#include <unordered_map>

namespace west::scene
{

namespace
{

using Clock = std::chrono::steady_clock;

constexpr uint32_t kSceneCacheVersion = 5;
constexpr float kDefaultOpacityAlphaCutoff = 0.08f;

struct GeometryCounters
{
    uint32_t meshCount = 0;
    uint32_t instanceCount = 0;
    uint64_t vertexCount = 0;
    uint64_t indexCount = 0;
};

struct SceneCacheHeader
{
    uint32_t version = kSceneCacheVersion;
    float uniformScale = 1.0f;
    int64_t sourceWriteTime = 0;
    int64_t materialWriteTime = 0;
    uint32_t sourceMeshCount = 0;
    uint32_t sourceInstanceCount = 0;
    uint64_t sourceVertexCount = 0;
    uint64_t sourceIndexCount = 0;
    uint32_t optimizedMeshCount = 0;
    uint32_t optimizedInstanceCount = 0;
    uint64_t optimizedVertexCount = 0;
    uint64_t optimizedIndexCount = 0;
    uint32_t appliedMeshMerge = 0;
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

[[nodiscard]] std::array<float, 16> ToRowMajorArray(const aiMatrix4x4& matrix)
{
    return {
        matrix.a1, matrix.a2, matrix.a3, matrix.a4, matrix.b1, matrix.b2, matrix.b3, matrix.b4,
        matrix.c1, matrix.c2, matrix.c3, matrix.c4, matrix.d1, matrix.d2, matrix.d3, matrix.d4,
    };
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

[[nodiscard]] std::filesystem::path GetSceneMaterialPath(const std::filesystem::path& sourceFile)
{
    std::filesystem::path materialPath = sourceFile;
    materialPath.replace_extension(".mtl");
    return materialPath;
}

[[nodiscard]] int64_t GetLastWriteTimeTicks(const std::filesystem::path& path)
{
    std::error_code errorCode;
    const auto timestamp = std::filesystem::last_write_time(path, errorCode);
    if (errorCode)
    {
        return 0;
    }

    return static_cast<int64_t>(timestamp.time_since_epoch().count());
}

[[nodiscard]] std::filesystem::path BuildSceneCachePath(const std::filesystem::path& sourceFile)
{
    return sourceFile.parent_path() / std::format("{}.westscene.bin", sourceFile.stem().string());
}

[[nodiscard]] float Clamp01(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

[[nodiscard]] float RoughnessFromShininess(float shininess)
{
    if (shininess <= 0.0f)
    {
        return 0.9f;
    }
    return Clamp01(std::sqrt(2.0f / (shininess + 2.0f)));
}

template <typename T>
bool WriteValue(std::ofstream& stream, const T& value)
{
    stream.write(reinterpret_cast<const char*>(&value), sizeof(T));
    return stream.good();
}

template <typename T>
bool ReadValue(std::ifstream& stream, T& value)
{
    stream.read(reinterpret_cast<char*>(&value), sizeof(T));
    return stream.good();
}

bool WriteString(std::ofstream& stream, std::string_view value)
{
    const uint32_t length = static_cast<uint32_t>(value.size());
    if (!WriteValue(stream, length))
    {
        return false;
    }

    if (length == 0)
    {
        return true;
    }

    stream.write(value.data(), length);
    return stream.good();
}

bool ReadString(std::ifstream& stream, std::string& value)
{
    uint32_t length = 0;
    if (!ReadValue(stream, length))
    {
        return false;
    }

    value.resize(length);
    if (length == 0)
    {
        return true;
    }

    stream.read(value.data(), length);
    return stream.good();
}

bool WritePath(std::ofstream& stream, const std::filesystem::path& path)
{
    return WriteString(stream, path.generic_string());
}

bool ReadPath(std::ifstream& stream, std::filesystem::path& path)
{
    std::string pathString;
    if (!ReadString(stream, pathString))
    {
        return false;
    }

    path = std::filesystem::path(pathString);
    return true;
}

template <typename T>
bool WriteVector(std::ofstream& stream, const std::vector<T>& values)
{
    const uint64_t count = static_cast<uint64_t>(values.size());
    if (!WriteValue(stream, count))
    {
        return false;
    }

    if (count == 0)
    {
        return true;
    }

    stream.write(reinterpret_cast<const char*>(values.data()), static_cast<std::streamsize>(count * sizeof(T)));
    return stream.good();
}

template <typename T>
bool ReadVector(std::ifstream& stream, std::vector<T>& values)
{
    uint64_t count = 0;
    if (!ReadValue(stream, count))
    {
        return false;
    }

    values.resize(static_cast<size_t>(count));
    if (count == 0)
    {
        return true;
    }

    stream.read(reinterpret_cast<char*>(values.data()), static_cast<std::streamsize>(count * sizeof(T)));
    return stream.good();
}

bool WriteTextureAsset(std::ofstream& stream, const TextureAsset& texture)
{
    return WritePath(stream, texture.sourcePath) && WriteString(stream, texture.debugName);
}

bool ReadTextureAsset(std::ifstream& stream, TextureAsset& texture)
{
    return ReadPath(stream, texture.sourcePath) && ReadString(stream, texture.debugName);
}

bool WriteMaterial(std::ofstream& stream, const Material& material)
{
    stream.write(reinterpret_cast<const char*>(material.baseColor.data()),
                 static_cast<std::streamsize>(material.baseColor.size() * sizeof(float)));
    return stream.good() && WriteValue(stream, material.baseColorTextureIndex) &&
           WriteValue(stream, material.opacityTextureIndex) && WriteValue(stream, material.roughness) &&
           WriteValue(stream, material.metallic) && WriteValue(stream, material.alphaCutoff) &&
           WriteString(stream, material.debugName);
}

bool ReadMaterial(std::ifstream& stream, Material& material)
{
    stream.read(reinterpret_cast<char*>(material.baseColor.data()),
                static_cast<std::streamsize>(material.baseColor.size() * sizeof(float)));
    return stream.good() && ReadValue(stream, material.baseColorTextureIndex) &&
           ReadValue(stream, material.opacityTextureIndex) && ReadValue(stream, material.roughness) &&
           ReadValue(stream, material.metallic) && ReadValue(stream, material.alphaCutoff) &&
           ReadString(stream, material.debugName);
}

bool WriteMeshData(std::ofstream& stream, const MeshData& mesh)
{
    return WriteVector(stream, mesh.vertices) && WriteVector(stream, mesh.indices) &&
           WriteValue(stream, mesh.materialIndex) && WriteString(stream, mesh.debugName);
}

bool ReadMeshData(std::ifstream& stream, MeshData& mesh)
{
    return ReadVector(stream, mesh.vertices) && ReadVector(stream, mesh.indices) &&
           ReadValue(stream, mesh.materialIndex) && ReadString(stream, mesh.debugName);
}

bool WriteInstanceData(std::ofstream& stream, const InstanceData& instance)
{
    stream.write(reinterpret_cast<const char*>(instance.modelMatrix.data()),
                 static_cast<std::streamsize>(instance.modelMatrix.size() * sizeof(float)));
    return stream.good() && WriteValue(stream, instance.meshIndex);
}

bool ReadInstanceData(std::ifstream& stream, InstanceData& instance)
{
    stream.read(reinterpret_cast<char*>(instance.modelMatrix.data()),
                static_cast<std::streamsize>(instance.modelMatrix.size() * sizeof(float)));
    return stream.good() && ReadValue(stream, instance.meshIndex);
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

bool WriteSceneCache(const std::filesystem::path& cachePath, const std::filesystem::path& sourceFile,
                     const SceneLoadOptions& options, const SceneAsset& asset, const SceneLoadStats& loadStats)
{
    std::ofstream stream(cachePath, std::ios::binary | std::ios::trunc);
    if (!stream.is_open())
    {
        return false;
    }

    SceneCacheHeader header{};
    header.version = kSceneCacheVersion;
    header.uniformScale = options.uniformScale;
    header.sourceWriteTime = GetLastWriteTimeTicks(sourceFile);
    header.materialWriteTime = GetLastWriteTimeTicks(GetSceneMaterialPath(sourceFile));
    header.sourceMeshCount = loadStats.sourceMeshCount;
    header.sourceInstanceCount = loadStats.sourceInstanceCount;
    header.sourceVertexCount = loadStats.sourceVertexCount;
    header.sourceIndexCount = loadStats.sourceIndexCount;
    header.optimizedMeshCount = loadStats.optimizedMeshCount;
    header.optimizedInstanceCount = loadStats.optimizedInstanceCount;
    header.optimizedVertexCount = loadStats.optimizedVertexCount;
    header.optimizedIndexCount = loadStats.optimizedIndexCount;
    header.appliedMeshMerge = loadStats.appliedMeshMerge ? 1u : 0u;

    if (!WriteValue(stream, header) || !WriteString(stream, asset.GetDebugName()))
    {
        return false;
    }

    const uint64_t textureCount = static_cast<uint64_t>(asset.GetTextures().size());
    if (!WriteValue(stream, textureCount))
    {
        return false;
    }
    for (const TextureAsset& texture : asset.GetTextures())
    {
        if (!WriteTextureAsset(stream, texture))
        {
            return false;
        }
    }

    const uint64_t materialCount = static_cast<uint64_t>(asset.GetMaterials().size());
    if (!WriteValue(stream, materialCount))
    {
        return false;
    }
    for (const Material& material : asset.GetMaterials())
    {
        if (!WriteMaterial(stream, material))
        {
            return false;
        }
    }

    const uint64_t meshCount = static_cast<uint64_t>(asset.GetMeshes().size());
    if (!WriteValue(stream, meshCount))
    {
        return false;
    }
    for (const MeshData& mesh : asset.GetMeshes())
    {
        if (!WriteMeshData(stream, mesh))
        {
            return false;
        }
    }

    const uint64_t instanceCount = static_cast<uint64_t>(asset.GetInstances().size());
    if (!WriteValue(stream, instanceCount))
    {
        return false;
    }
    for (const InstanceData& instance : asset.GetInstances())
    {
        if (!WriteInstanceData(stream, instance))
        {
            return false;
        }
    }

    stream.write(reinterpret_cast<const char*>(asset.GetBoundsMin().data()),
                 static_cast<std::streamsize>(asset.GetBoundsMin().size() * sizeof(float)));
    stream.write(reinterpret_cast<const char*>(asset.GetBoundsMax().data()),
                 static_cast<std::streamsize>(asset.GetBoundsMax().size() * sizeof(float)));
    return stream.good();
}

bool TryReadSceneCache(const std::filesystem::path& cachePath, const std::filesystem::path& sourceFile,
                       const SceneLoadOptions& options, SceneAsset& asset, SceneLoadStats& loadStats)
{
    std::ifstream stream(cachePath, std::ios::binary);
    if (!stream.is_open())
    {
        return false;
    }

    SceneCacheHeader header{};
    if (!ReadValue(stream, header))
    {
        return false;
    }

    if (header.version != kSceneCacheVersion ||
        std::bit_cast<uint32_t>(header.uniformScale) != std::bit_cast<uint32_t>(options.uniformScale) ||
        header.sourceWriteTime != GetLastWriteTimeTicks(sourceFile) ||
        header.materialWriteTime != GetLastWriteTimeTicks(GetSceneMaterialPath(sourceFile)))
    {
        return false;
    }

    std::string debugName;
    if (!ReadString(stream, debugName))
    {
        return false;
    }
    asset.SetDebugName(std::move(debugName));

    uint64_t textureCount = 0;
    if (!ReadValue(stream, textureCount))
    {
        return false;
    }
    for (uint64_t i = 0; i < textureCount; ++i)
    {
        TextureAsset texture{};
        if (!ReadTextureAsset(stream, texture))
        {
            return false;
        }
        asset.AddTexture(std::move(texture));
    }

    uint64_t materialCount = 0;
    if (!ReadValue(stream, materialCount))
    {
        return false;
    }
    for (uint64_t i = 0; i < materialCount; ++i)
    {
        Material material{};
        if (!ReadMaterial(stream, material))
        {
            return false;
        }
        asset.AddMaterial(std::move(material));
    }

    uint64_t meshCount = 0;
    if (!ReadValue(stream, meshCount))
    {
        return false;
    }
    for (uint64_t i = 0; i < meshCount; ++i)
    {
        MeshData mesh{};
        if (!ReadMeshData(stream, mesh))
        {
            return false;
        }
        asset.AddMesh(std::move(mesh));
    }

    uint64_t instanceCount = 0;
    if (!ReadValue(stream, instanceCount))
    {
        return false;
    }
    for (uint64_t i = 0; i < instanceCount; ++i)
    {
        InstanceData instance{};
        if (!ReadInstanceData(stream, instance))
        {
            return false;
        }
        asset.AddInstance(std::move(instance));
    }

    std::array<float, 3> boundsMin{};
    std::array<float, 3> boundsMax{};
    stream.read(reinterpret_cast<char*>(boundsMin.data()),
                static_cast<std::streamsize>(boundsMin.size() * sizeof(float)));
    stream.read(reinterpret_cast<char*>(boundsMax.data()),
                static_cast<std::streamsize>(boundsMax.size() * sizeof(float)));
    if (!stream.good())
    {
        return false;
    }

    asset.SetBounds(boundsMin, boundsMax);

    loadStats.usedCache = true;
    loadStats.appliedMeshMerge = header.appliedMeshMerge != 0;
    loadStats.sourceMeshCount = header.sourceMeshCount;
    loadStats.sourceInstanceCount = header.sourceInstanceCount;
    loadStats.sourceVertexCount = header.sourceVertexCount;
    loadStats.sourceIndexCount = header.sourceIndexCount;
    loadStats.optimizedMeshCount = header.optimizedMeshCount;
    loadStats.optimizedInstanceCount = header.optimizedInstanceCount;
    loadStats.optimizedVertexCount = header.optimizedVertexCount;
    loadStats.optimizedIndexCount = header.optimizedIndexCount;
    return true;
}

[[nodiscard]] uint32_t ResolveTextureIndex(const std::filesystem::path& sceneSourceFile, std::string_view texturePath,
                                           std::unordered_map<std::string, uint32_t>& textureIndices, SceneAsset& asset)
{
    if (texturePath.empty())
    {
        return kInvalidSceneTextureIndex;
    }

    const std::filesystem::path resolvedPath = NormalizePath(sceneSourceFile.parent_path() / texturePath);
    const std::string textureKey = resolvedPath.generic_string();
    const auto existing = textureIndices.find(textureKey);
    if (existing != textureIndices.end())
    {
        return existing->second;
    }

    TextureAsset texture{};
    texture.sourcePath = resolvedPath;
    texture.debugName = resolvedPath.filename().string();
    const uint32_t textureIndex = asset.AddTexture(std::move(texture));
    textureIndices.emplace(textureKey, textureIndex);
    return textureIndex;
}

[[nodiscard]] uint32_t ResolveBaseColorTextureIndex(const std::filesystem::path& sceneSourceFile,
                                                    const aiMaterial* material,
                                                    std::unordered_map<std::string, uint32_t>& textureIndices,
                                                    SceneAsset& asset)
{
    aiString texturePath;
    if (material->GetTextureCount(aiTextureType_BASE_COLOR) > 0 &&
        material->GetTexture(aiTextureType_BASE_COLOR, 0, &texturePath) == aiReturn_SUCCESS)
    {
        // Prefer base-color when available.
    }
    else if (material->GetTextureCount(aiTextureType_DIFFUSE) > 0 &&
             material->GetTexture(aiTextureType_DIFFUSE, 0, &texturePath) == aiReturn_SUCCESS)
    {
        // OBJ/MTL fallback.
    }
    else
    {
        return kInvalidSceneTextureIndex;
    }

    return ResolveTextureIndex(sceneSourceFile, texturePath.C_Str(), textureIndices, asset);
}

[[nodiscard]] uint32_t ResolveOpacityTextureIndex(const std::filesystem::path& sceneSourceFile,
                                                  const aiMaterial* material,
                                                  std::unordered_map<std::string, uint32_t>& textureIndices,
                                                  SceneAsset& asset)
{
    aiString texturePath;
    if (material->GetTextureCount(aiTextureType_OPACITY) == 0 ||
        material->GetTexture(aiTextureType_OPACITY, 0, &texturePath) != aiReturn_SUCCESS)
    {
        return kInvalidSceneTextureIndex;
    }

    return ResolveTextureIndex(sceneSourceFile, texturePath.C_Str(), textureIndices, asset);
}

[[nodiscard]] Material ConvertMaterial(const std::filesystem::path& sceneSourceFile, const aiMaterial* material,
                                       std::unordered_map<std::string, uint32_t>& textureIndices, SceneAsset& asset)
{
    Material converted{};

    aiString materialName;
    if (material->Get(AI_MATKEY_NAME, materialName) == aiReturn_SUCCESS)
    {
        converted.debugName = materialName.C_Str();
    }

    aiColor4D baseColor{};
    if (aiGetMaterialColor(material, AI_MATKEY_BASE_COLOR, &baseColor) == aiReturn_SUCCESS ||
        aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &baseColor) == aiReturn_SUCCESS)
    {
        converted.baseColor = {baseColor.r, baseColor.g, baseColor.b, baseColor.a};
    }

    float roughness = 0.0f;
    if (aiGetMaterialFloat(material, AI_MATKEY_ROUGHNESS_FACTOR, &roughness) == aiReturn_SUCCESS)
    {
        converted.roughness = Clamp01(roughness);
    }
    else
    {
        float shininess = 0.0f;
        if (aiGetMaterialFloat(material, AI_MATKEY_SHININESS, &shininess) == aiReturn_SUCCESS)
        {
            converted.roughness = RoughnessFromShininess(shininess);
        }
        else
        {
            converted.roughness = 0.9f;
        }
    }

    float metallic = 0.0f;
    if (aiGetMaterialFloat(material, AI_MATKEY_METALLIC_FACTOR, &metallic) == aiReturn_SUCCESS)
    {
        converted.metallic = Clamp01(metallic);
    }
    else
    {
        converted.metallic = 0.0f;
    }

    converted.baseColorTextureIndex = ResolveBaseColorTextureIndex(sceneSourceFile, material, textureIndices, asset);
    converted.opacityTextureIndex = ResolveOpacityTextureIndex(sceneSourceFile, material, textureIndices, asset);

    aiString alphaMode;
    if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == aiReturn_SUCCESS &&
        std::string_view(alphaMode.C_Str()) == "MASK")
    {
        ai_real alphaCutoff = 0.5f;
        if (material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, alphaCutoff) != aiReturn_SUCCESS)
        {
            alphaCutoff = 0.5f;
        }
        converted.alphaCutoff = Clamp01(static_cast<float>(alphaCutoff));
    }
    else if (converted.opacityTextureIndex != kInvalidSceneTextureIndex)
    {
        converted.alphaCutoff = kDefaultOpacityAlphaCutoff;
    }
    return converted;
}

[[nodiscard]] MeshData ConvertMesh(const aiMesh* mesh)
{
    MeshData converted{};
    converted.materialIndex = mesh->mMaterialIndex;
    converted.debugName = mesh->mName.length > 0 ? mesh->mName.C_Str() : "StaticMesh";
    converted.vertices.resize(mesh->mNumVertices);

    for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex)
    {
        MeshVertex& vertex = converted.vertices[vertexIndex];
        vertex.position[0] = mesh->mVertices[vertexIndex].x;
        vertex.position[1] = mesh->mVertices[vertexIndex].y;
        vertex.position[2] = mesh->mVertices[vertexIndex].z;

        if (mesh->HasNormals())
        {
            vertex.normal[0] = mesh->mNormals[vertexIndex].x;
            vertex.normal[1] = mesh->mNormals[vertexIndex].y;
            vertex.normal[2] = mesh->mNormals[vertexIndex].z;
        }
        else
        {
            vertex.normal[0] = 0.0f;
            vertex.normal[1] = 1.0f;
            vertex.normal[2] = 0.0f;
        }

        if (mesh->HasTextureCoords(0))
        {
            vertex.uv[0] = mesh->mTextureCoords[0][vertexIndex].x;
            vertex.uv[1] = 1.0f - mesh->mTextureCoords[0][vertexIndex].y;
        }
        else
        {
            vertex.uv[0] = 0.0f;
            vertex.uv[1] = 0.0f;
        }
    }

    converted.indices.reserve(static_cast<size_t>(mesh->mNumFaces) * 3);
    for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex)
    {
        const aiFace& face = mesh->mFaces[faceIndex];
        if (face.mNumIndices != 3)
        {
            continue;
        }

        converted.indices.push_back(face.mIndices[0]);
        converted.indices.push_back(face.mIndices[1]);
        converted.indices.push_back(face.mIndices[2]);
    }

    return converted;
}

void AddNodeInstances(const aiNode* node, const aiMatrix4x4& parentTransform, const std::array<float, 16>& sceneScale,
                      SceneAsset& asset)
{
    const aiMatrix4x4 worldTransform = parentTransform * node->mTransformation;
    const std::array<float, 16> rowMajorTransform = MultiplyMatrix4x4(ToRowMajorArray(worldTransform), sceneScale);

    for (uint32_t meshSlot = 0; meshSlot < node->mNumMeshes; ++meshSlot)
    {
        InstanceData instance{};
        instance.meshIndex = node->mMeshes[meshSlot];
        instance.modelMatrix = rowMajorTransform;
        asset.AddInstance(instance);
    }

    for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex)
    {
        AddNodeInstances(node->mChildren[childIndex], worldTransform, sceneScale, asset);
    }
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
            const std::array<float, 3> transformed = TransformPointAffine(instance.modelMatrix, vertex.position);
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

} // namespace

SceneAsset SceneAssetLoader::LoadStaticScene(const std::filesystem::path& sourceFile, const SceneLoadOptions& options)
{
    const auto totalStart = Clock::now();
    const std::filesystem::path normalizedSource = NormalizePath(sourceFile);
    if (!std::filesystem::exists(normalizedSource))
    {
        WEST_LOG_FATAL(LogCategory::Scene, "Scene file not found: {}", normalizedSource.string());
        WEST_CHECK(false, "Scene source file is missing");
    }

    const std::string extension = normalizedSource.extension().string();
    if (extension == ".gltf" || extension == ".glb")
    {
        return MeshLoader::LoadStaticSceneGltf(normalizedSource, options);
    }

    SceneLoadStats loadStats{};
    SceneAsset asset{};
    asset.SetDebugName(normalizedSource.stem().string());

    const std::filesystem::path cachePath = BuildSceneCachePath(normalizedSource);
    Logger::Log(LogLevel::Info, LogCategory::Scene,
                std::format("Loading static scene {} (cache={}, merge={}, scale={:.4f}).", normalizedSource.string(),
                            options.enableCache, options.enableStaticMeshMerge, options.uniformScale));

    if (options.enableCache)
    {
        const auto cacheReadStart = Clock::now();
        if (TryReadSceneCache(cachePath, normalizedSource, options, asset, loadStats))
        {
            loadStats.cacheReadMs = std::chrono::duration<double, std::milli>(Clock::now() - cacheReadStart).count();
            loadStats.totalLoadMs = std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
            asset.SetLoadStats(loadStats);

            Logger::Log(LogLevel::Info, LogCategory::Scene,
                        std::format("Loaded static scene cache {} (meshes={}, materials={}, textures={}, instances={}, "
                                    "{:.2f} ms).",
                                    cachePath.string(), asset.GetMeshes().size(), asset.GetMaterials().size(),
                                    asset.GetTextures().size(), asset.GetInstances().size(), loadStats.totalLoadMs));
            return asset;
        }

        Logger::Log(LogLevel::Info, LogCategory::Scene,
                    std::format("Scene cache miss or stale cache for {}.", cachePath.string()));
    }

    const auto importStart = Clock::now();

    Assimp::Importer importer;
    const unsigned int importFlags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                     aiProcess_GenSmoothNormals | aiProcess_SortByPType |
                                     aiProcess_ImproveCacheLocality | aiProcess_RemoveRedundantMaterials;

    const aiScene* importedScene = importer.ReadFile(normalizedSource.string(), importFlags);
    if (importedScene == nullptr || !importedScene->HasMeshes() || importedScene->mRootNode == nullptr)
    {
        WEST_LOG_FATAL(LogCategory::Scene, "Failed to import scene {}: {}", normalizedSource.string(),
                       importer.GetErrorString());
        WEST_CHECK(false, "Scene import failed");
    }

    std::unordered_map<std::string, uint32_t> textureIndices;
    const uint32_t materialCount = importedScene->HasMaterials() ? importedScene->mNumMaterials : 0;
    if (materialCount == 0)
    {
        Material defaultMaterial{};
        defaultMaterial.debugName = "DefaultMaterial";
        asset.AddMaterial(std::move(defaultMaterial));
    }
    else
    {
        for (uint32_t materialIndex = 0; materialIndex < materialCount; ++materialIndex)
        {
            asset.AddMaterial(
                ConvertMaterial(normalizedSource, importedScene->mMaterials[materialIndex], textureIndices, asset));
        }
    }

    for (uint32_t meshIndex = 0; meshIndex < importedScene->mNumMeshes; ++meshIndex)
    {
        asset.AddMesh(ConvertMesh(importedScene->mMeshes[meshIndex]));
    }

    AddNodeInstances(importedScene->mRootNode, aiMatrix4x4(), UniformScale(options.uniformScale), asset);
    if (asset.GetInstances().empty())
    {
        for (uint32_t meshIndex = 0; meshIndex < asset.GetMeshes().size(); ++meshIndex)
        {
            InstanceData instance{};
            instance.meshIndex = meshIndex;
            instance.modelMatrix = UniformScale(options.uniformScale);
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

    if (options.enableCache)
    {
        const auto cacheWriteStart = Clock::now();
        loadStats.cacheWritten = WriteSceneCache(cachePath, normalizedSource, options, asset, loadStats);
        loadStats.cacheWriteMs = std::chrono::duration<double, std::milli>(Clock::now() - cacheWriteStart).count();
    }

    loadStats.totalLoadMs = std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();
    asset.SetLoadStats(loadStats);

    Logger::Log(LogLevel::Info, LogCategory::Scene,
                std::format("Loaded static scene {} (meshes {}->{}, instances {}->{}, materials={}, textures={}, "
                            "{:.2f} ms).",
                            normalizedSource.string(), loadStats.sourceMeshCount, loadStats.optimizedMeshCount,
                            loadStats.sourceInstanceCount, loadStats.optimizedInstanceCount,
                            asset.GetMaterials().size(), asset.GetTextures().size(), loadStats.totalLoadMs));

    return asset;
}

SceneAsset SceneAssetLoader::LoadStaticScene(const std::filesystem::path& sourceFile, float uniformScale)
{
    SceneLoadOptions options{};
    options.uniformScale = uniformScale;
    return LoadStaticScene(sourceFile, options);
}

SceneAsset SceneAssetLoader::LoadAmazonLumberyardBistro(const std::filesystem::path& assetRoot,
                                                        const SceneLoadOptions& options)
{
    SceneLoadOptions bistroOptions = options;
    bistroOptions.uniformScale = 0.01f;

    SceneAsset asset = LoadStaticScene(assetRoot / "Exterior" / "exterior.obj", bistroOptions);
    asset.SetDebugName("AmazonLumberyardBistro");
    return asset;
}

} // namespace west::scene
