// =============================================================================
// WestEngine - Shaders
// Deferred G-Buffer population pass
// =============================================================================

import Common.FrameConstants;
import Common.GlobalBindless;

struct MaterialData
{
    float4 baseColor;
    DescriptorHandle<Texture2D<float4>> baseColorTexture;
    DescriptorHandle<Texture2D<float4>> opacityTexture;
    float roughness;
    float metallic;
    float alphaCutoff;
    uint hasBaseColorTexture;
    uint hasOpacityTexture;
    uint padding0;
    uint padding1;
    uint padding2;
};

struct GBufferPushConstants
{
    DescriptorHandle<StructuredBuffer<FrameConstants>> frameData;
    DescriptorHandle<StructuredBuffer<MaterialData>> materialData;
    DescriptorHandle<StructuredBuffer<SceneDrawRecord>> drawData;
    DescriptorHandle<SamplerState> sampler;
    uint drawRecordIndexOverride;
    uint useDrawRecordIndexOverride;
};

[[vk::push_constant]]
ConstantBuffer<GBufferPushConstants> g_push;

struct SceneDrawRecord
{
    uint materialIndex;
    uint indexCount;
    uint firstIndex;
    int vertexOffset;
    row_major float4x4 model;
    float4 boundsSphere;
    float4 boundsMin;
    float4 boundsMax;
};

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD;
    uint drawRecordIndex : SV_StartInstanceLocation;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 worldPosition : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 uv : TEXCOORD2;
    nointerpolation uint materialIndex : TEXCOORD3;
};

struct PSOutput
{
    float4 worldPosition : SV_Target0;
    float4 normalRoughness : SV_Target1;
    float4 albedoMetallic : SV_Target2;
};

[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    FrameConstants frame = nonuniform(g_push.frameData)[0];
    uint drawRecordIndex =
        g_push.useDrawRecordIndexOverride != 0 ? g_push.drawRecordIndexOverride : input.drawRecordIndex;
    SceneDrawRecord draw = nonuniform(g_push.drawData)[drawRecordIndex];

    float4 worldPosition = mul(float4(input.position, 1.0f), draw.model);
    float3 worldNormal = normalize(mul(float4(input.normal, 0.0f), draw.model).xyz);

    VSOutput output;
    output.position = mul(worldPosition, frame.viewProjection);
    output.worldPosition = worldPosition.xyz;
    output.worldNormal = worldNormal;
    output.uv = input.uv;
    output.materialIndex = draw.materialIndex;
    return output;
}

[shader("fragment")]
PSOutput PSMain(VSOutput input)
{
    FrameConstants frame = nonuniform(g_push.frameData)[0];
    MaterialData material = nonuniform(g_push.materialData)[input.materialIndex];
    bool texturesEnabled = frame.materialParams.w > 0.5f;
    float4 baseColorSample = 1.0f.xxxx;
    if (material.hasBaseColorTexture != 0 && (texturesEnabled || material.alphaCutoff > 0.0f))
    {
        baseColorSample = nonuniform(material.baseColorTexture)->Sample(nonuniform(g_push.sampler), input.uv);
    }

    float opacity = material.baseColor.a * baseColorSample.a;
    if (material.hasOpacityTexture != 0)
    {
        opacity *= nonuniform(material.opacityTexture)->Sample(nonuniform(g_push.sampler), input.uv).r;
    }
    if (frame.renderFeatureFlags.w > 0.5f && material.alphaCutoff > 0.0f && opacity < material.alphaCutoff)
    {
        discard;
    }

    float3 albedo = material.baseColor.rgb * (texturesEnabled ? baseColorSample.rgb : 1.0f.xxx);

    float roughnessScale = max(frame.pbrParams.y, 0.0f);
    float metallicScale = max(frame.pbrParams.x, 0.0f);
    float roughness = saturate(material.roughness * roughnessScale);
    float metallic = saturate(material.metallic * metallicScale);
    float3 worldNormal = normalize(input.worldNormal);
    if (material.alphaCutoff > 0.0f && dot(worldNormal, frame.cameraPosition.xyz - input.worldPosition) < 0.0f)
    {
        worldNormal = -worldNormal;
    }

    PSOutput output;
    output.worldPosition = float4(input.worldPosition, 1.0f);
    output.normalRoughness = float4(worldNormal * 0.5f + 0.5f.xxx, roughness);
    output.albedoMetallic = float4(albedo, metallic);
    return output;
}
