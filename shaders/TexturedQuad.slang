// =============================================================================
// WestEngine - Shaders
// Bindless textured quad smoke-test shader
// =============================================================================

import Common.GlobalBindless;

struct TexturedQuadPushConstants
{
    DescriptorHandle<Texture2D<float4>> texture;
    DescriptorHandle<SamplerState> sampler;
};

[[vk::push_constant]]
ConstantBuffer<TexturedQuadPushConstants> g_push;

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
    VSOutput output;
    output.position = float4(input.position, 1.0f);
    output.uv = input.uv;
    return output;
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target0
{
    return nonuniform(g_push.texture)->Sample(nonuniform(g_push.sampler), input.uv);
}
