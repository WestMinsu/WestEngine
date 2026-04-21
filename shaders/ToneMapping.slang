// =============================================================================
// WestEngine - Shaders
// Full-screen HDR tone mapping pass
// =============================================================================

import Common.GlobalBindless;

struct ToneMappingPushConstants
{
    DescriptorHandle<Texture2D<float4>> texture;
    DescriptorHandle<SamplerState> sampler;
};

[[vk::push_constant]]
ConstantBuffer<ToneMappingPushConstants> g_push;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VSOutput VSMain(uint vertexID : SV_VertexID)
{
    float2 positions[3] =
    {
        float2(-1.0f, -1.0f),
        float2( 3.0f, -1.0f),
        float2(-1.0f,  3.0f),
    };

    float2 uvs[3] =
    {
        float2(0.0f, 1.0f),
        float2(2.0f, 1.0f),
        float2(0.0f, -1.0f),
    };

    VSOutput output;
    output.position = float4(positions[vertexID], 0.0f, 1.0f);
    output.uv = uvs[vertexID];
    return output;
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target0
{
    const float3 hdrColor = nonuniform(g_push.texture)->Sample(nonuniform(g_push.sampler), input.uv).rgb;
    const float3 reinhard = hdrColor / (1.0f + hdrColor);
    const float3 gammaCorrected = pow(max(reinhard, 0.0f.xxx), 1.0f / 2.2f);
    return float4(gammaCorrected, 1.0f);
}
