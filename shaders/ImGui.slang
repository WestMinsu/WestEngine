// =============================================================================
// WestEngine - Shaders
// Dear ImGui overlay pass
// =============================================================================

import Common.GlobalBindless;

struct ImGuiPushConstants
{
    float2 scale;
    float2 translate;
    DescriptorHandle<Texture2D<float4>> texture;
    DescriptorHandle<SamplerState> sampler;
};

[[vk::push_constant]]
ConstantBuffer<ImGuiPushConstants> g_push;

struct VSInput
{
    float2 position : POSITION;
    float2 uv : TEXCOORD;
    float4 color : COLOR;
};

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.position = float4(input.position * g_push.scale + g_push.translate, 0.0f, 1.0f);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target
{
    Texture2D<float4> fontTexture = nonuniform(g_push.texture);
    SamplerState fontSampler = nonuniform(g_push.sampler);
    return input.color * fontTexture.Sample(fontSampler, input.uv);
}
