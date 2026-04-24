// =============================================================================
// WestEngine - Shaders
// Directional shadow map pass
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

struct ShadowMapPushConstants
{
    DescriptorHandle<StructuredBuffer<FrameConstants>> frameData;
    DescriptorHandle<StructuredBuffer<MaterialData>> materialData;
    DescriptorHandle<SamplerState> sampler;
    uint materialIndex;
    uint padding0;
    row_major float4x4 model;
};

[[vk::push_constant]]
ConstantBuffer<ShadowMapPushConstants> g_push;

struct VSInput
{
    float3 position : POSITION;
    float2 uv : TEXCOORD;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    FrameConstants frame = nonuniform(g_push.frameData)[0];
    float4 worldPosition = mul(float4(input.position, 1.0f), g_push.model);
    VSOutput output;
    output.position = mul(worldPosition, frame.lightViewProjection);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
void PSMain(VSOutput input)
{
    FrameConstants frame = nonuniform(g_push.frameData)[0];
    MaterialData material = nonuniform(g_push.materialData)[g_push.materialIndex];

    float opacity = material.baseColor.a;
    if (material.hasBaseColorTexture != 0)
    {
        opacity *= nonuniform(material.baseColorTexture)->Sample(nonuniform(g_push.sampler), input.uv).a;
    }
    if (material.hasOpacityTexture != 0)
    {
        opacity *= nonuniform(material.opacityTexture)->Sample(nonuniform(g_push.sampler), input.uv).r;
    }
    if (frame.renderFeatureFlags.w > 0.5f && material.alphaCutoff > 0.0f && opacity < material.alphaCutoff)
    {
        discard;
    }
}
