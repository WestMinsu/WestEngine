// =============================================================================
// WestEngine - Shaders
// Full-screen post pass with ACES tone mapping and FXAA baseline
// =============================================================================

import Common.GlobalBindless;

struct ToneMappingPushConstants
{
    DescriptorHandle<Texture2D<float4>> texture;
    DescriptorHandle<SamplerState> sampler;
    float4 colorGrading;
    float4 postEffects0;
    float4 postEffects1;
    uint4 controls;
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

float3 ApplyACESToneMapping(float3 color)
{
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((color * (a * color + b)) / (color * (c * color + d) + e));
}

float3 ApplyReinhardToneMapping(float3 color)
{
    return color / (color + 1.0f.xxx);
}

float3 ApplyUncharted2ToneMapping(float3 color)
{
    const float A = 0.15f;
    const float B = 0.50f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.02f;
    const float F = 0.30f;
    const float W = 11.2f;

    float3 curr = ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
    float3 whiteScale =
        ((W.xxx * (A * W.xxx + C * B) + D * E) / (W.xxx * (A * W.xxx + B) + D * F)) - E / F;
    return curr / max(whiteScale, 1e-4f.xxx);
}

float3 ApplyGranTurismoToneMapping(float3 color)
{
    const float P = 1.0f;
    const float a = 1.0f;
    const float m = 0.22f;
    const float l = 0.4f;
    const float c = 1.33f;
    const float b = 0.0f;

    float l0 = ((P - m) * l) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    float3 w0 = 1.0f.xxx - smoothstep(0.0f.xxx, m.xxx, color);
    float3 w2 = step((m + l0).xxx, color);
    float3 w1 = 1.0f.xxx - w0 - w2;

    float3 T = m.xxx * pow(max(color, 0.0f.xxx) / max(m, 1e-4f).xxx, c.xxx) + b.xxx;
    float3 S = P.xxx - (P - S1).xxx * exp(CP.xxx * (color - S0.xxx));
    float3 L = m.xxx + a.xxx * (color - m.xxx);
    return T * w0 + L * w1 + S * w2;
}

float3 ApplyLottesToneMapping(float3 color)
{
    const float3 a = 1.6f.xxx;
    const float3 d = 0.977f.xxx;
    const float3 hdrMax = 8.0f.xxx;
    const float3 midIn = 0.18f.xxx;
    const float3 midOut = 0.267f.xxx;

    float3 bTerm = (-pow(midIn, a) + pow(hdrMax, a) * midOut) /
                   ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    float3 cTerm = (pow(hdrMax, a * d) * pow(midIn, a) -
                    pow(hdrMax, a) * pow(midIn, a * d) * midOut) /
                   ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    return pow(max(color, 0.0f.xxx), a) / (pow(max(color, 0.0f.xxx), a * d) * bTerm + cTerm);
}

float3 ApplyExponentialToneMapping(float3 color)
{
    return 1.0f.xxx - exp(-color);
}

float3 ApplyReinhardExtendedToneMapping(float3 color, float maxWhite)
{
    float white = max(maxWhite, 1e-4f);
    float3 numerator = color * (1.0f.xxx + color / (white * white).xxx);
    return numerator / (1.0f.xxx + color);
}

float3 ApplyLuminanceToneMapping(float3 color)
{
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    return color / (1.0f + luminance).xxx;
}

float3 HableCurve(float3 color)
{
    const float A = 0.22f;
    const float B = 0.30f;
    const float C = 0.10f;
    const float D = 0.20f;
    const float E = 0.01f;
    const float F = 0.30f;

    return ((color * (A * color + C * B) + D * E) / (color * (A * color + B) + D * F)) - E / F;
}

float3 ApplyHableToneMapping(float3 color)
{
    const float exposureBias = 2.0f;
    float3 mapped = HableCurve(exposureBias * color);
    float3 whiteScale = 1.0f.xxx / max(HableCurve(11.2f.xxx), 1e-4f.xxx);
    return mapped * whiteScale;
}

float3 ApplyToneMappingOperator(float3 hdrColor, uint toneMappingOperator, float maxWhite)
{
    switch (toneMappingOperator)
    {
    case 1u:
        return ApplyReinhardToneMapping(hdrColor);
    case 2u:
        return ApplyACESToneMapping(hdrColor);
    case 3u:
        return ApplyUncharted2ToneMapping(hdrColor);
    case 4u:
        return ApplyGranTurismoToneMapping(hdrColor);
    case 5u:
        return ApplyLottesToneMapping(hdrColor);
    case 6u:
        return ApplyExponentialToneMapping(hdrColor);
    case 7u:
        return ApplyReinhardExtendedToneMapping(hdrColor, maxWhite);
    case 8u:
        return ApplyLuminanceToneMapping(hdrColor);
    case 9u:
        return ApplyHableToneMapping(hdrColor);
    default:
        return max(hdrColor, 0.0f.xxx);
    }
}

float3 SampleSourceColor(Texture2D<float4> sourceTexture, SamplerState sourceSampler, float2 uv, float chromaticAberration)
{
    if (chromaticAberration <= 0.0f)
    {
        return max(sourceTexture.SampleLevel(sourceSampler, uv, 0.0f).rgb, 0.0f.xxx);
    }

    float2 center = float2(0.5f, 0.5f);
    float2 offset = (uv - center) * chromaticAberration * 0.01f;
    float r = sourceTexture.SampleLevel(sourceSampler, uv + offset, 0.0f).r;
    float g = sourceTexture.SampleLevel(sourceSampler, uv, 0.0f).g;
    float b = sourceTexture.SampleLevel(sourceSampler, uv - offset, 0.0f).b;
    return max(float3(r, g, b), 0.0f.xxx);
}

float3 SampleToneMappedColor(Texture2D<float4> sourceTexture, SamplerState sourceSampler, float2 uv, float exposure,
                             float chromaticAberration, uint toneMappingOperator, float maxWhite)
{
    float3 hdrColor = SampleSourceColor(sourceTexture, sourceSampler, uv, chromaticAberration) * exposure;
    return ApplyToneMappingOperator(hdrColor, toneMappingOperator, maxWhite);
}

float ComputeFxaaLuma(float3 color)
{
    return dot(color, float3(0.299f, 0.587f, 0.114f));
}

float3 ApplyFXAA(Texture2D<float4> sourceTexture, SamplerState sourceSampler, float2 uv, float exposure,
                 float chromaticAberration, uint toneMappingOperator, float maxWhite)
{
    uint width = 0;
    uint height = 0;
    sourceTexture.GetDimensions(width, height);
    float2 texelSize = 1.0f / float2(max(width, 1u), max(height, 1u));

    float3 colorM =
        SampleToneMappedColor(sourceTexture, sourceSampler, uv, exposure, chromaticAberration, toneMappingOperator, maxWhite);
    float3 colorN = SampleToneMappedColor(sourceTexture, sourceSampler, uv + float2(0.0f, -texelSize.y), exposure,
                                          chromaticAberration, toneMappingOperator, maxWhite);
    float3 colorS = SampleToneMappedColor(sourceTexture, sourceSampler, uv + float2(0.0f, texelSize.y), exposure,
                                          chromaticAberration, toneMappingOperator, maxWhite);
    float3 colorW = SampleToneMappedColor(sourceTexture, sourceSampler, uv + float2(-texelSize.x, 0.0f), exposure,
                                          chromaticAberration, toneMappingOperator, maxWhite);
    float3 colorE = SampleToneMappedColor(sourceTexture, sourceSampler, uv + float2(texelSize.x, 0.0f), exposure,
                                          chromaticAberration, toneMappingOperator, maxWhite);
    float3 colorNW = SampleToneMappedColor(sourceTexture, sourceSampler, uv + float2(-texelSize.x, -texelSize.y), exposure,
                                           chromaticAberration, toneMappingOperator, maxWhite);
    float3 colorNE = SampleToneMappedColor(sourceTexture, sourceSampler, uv + float2(texelSize.x, -texelSize.y), exposure,
                                           chromaticAberration, toneMappingOperator, maxWhite);
    float3 colorSW = SampleToneMappedColor(sourceTexture, sourceSampler, uv + float2(-texelSize.x, texelSize.y), exposure,
                                           chromaticAberration, toneMappingOperator, maxWhite);
    float3 colorSE = SampleToneMappedColor(sourceTexture, sourceSampler, uv + float2(texelSize.x, texelSize.y), exposure,
                                           chromaticAberration, toneMappingOperator, maxWhite);

    float lumaM = ComputeFxaaLuma(colorM);
    float lumaN = ComputeFxaaLuma(colorN);
    float lumaS = ComputeFxaaLuma(colorS);
    float lumaW = ComputeFxaaLuma(colorW);
    float lumaE = ComputeFxaaLuma(colorE);
    float lumaNW = ComputeFxaaLuma(colorNW);
    float lumaNE = ComputeFxaaLuma(colorNE);
    float lumaSW = ComputeFxaaLuma(colorSW);
    float lumaSE = ComputeFxaaLuma(colorSE);

    float lumaMin = min(lumaM, min(min(lumaN, lumaS), min(min(lumaW, lumaE), min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)))));
    float lumaMax = max(lumaM, max(max(lumaN, lumaS), max(max(lumaW, lumaE), max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)))));
    float lumaRange = lumaMax - lumaMin;

    const float edgeThreshold = 0.125f;
    const float edgeThresholdMin = 0.03125f;
    if (lumaRange < max(edgeThresholdMin, lumaMax * edgeThreshold))
    {
        return colorM;
    }

    float edgeHorz = abs(lumaN + lumaS - 2.0f * lumaM) * 2.0f +
                     abs(lumaNW + lumaSW - 2.0f * lumaW) +
                     abs(lumaNE + lumaSE - 2.0f * lumaE);
    float edgeVert = abs(lumaW + lumaE - 2.0f * lumaM) * 2.0f +
                     abs(lumaNW + lumaNE - 2.0f * lumaN) +
                     abs(lumaSW + lumaSE - 2.0f * lumaS);

    bool isHorizontal = edgeHorz >= edgeVert;
    float2 blendDirection = isHorizontal ? float2(0.0f, texelSize.y) : float2(texelSize.x, 0.0f);
    float3 blendColor =
        (SampleToneMappedColor(sourceTexture, sourceSampler, uv - blendDirection, exposure, chromaticAberration,
                               toneMappingOperator, maxWhite) +
         colorM +
         SampleToneMappedColor(sourceTexture, sourceSampler, uv + blendDirection, exposure, chromaticAberration,
                               toneMappingOperator, maxWhite)) / 3.0f;

    float edgeStrength = saturate(lumaRange / max(lumaMax, 1e-4f));
    float blendFactor = 0.75f * smoothstep(0.0f, 1.0f, edgeStrength);
    return lerp(colorM, blendColor, blendFactor);
}

float3 ApplyChannelDebug(float3 color, uint debugChannel)
{
    switch (debugChannel)
    {
    case 1u:
        return color.rrr;
    case 2u:
        return color.ggg;
    case 3u:
        return color.bbb;
    case 4u:
        return 1.0f.xxx;
    case 5u:
    {
        float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
        return luminance.xxx;
    }
    default:
        return color;
    }
}

float3 ApplySplitDivider(float3 color, Texture2D<float4> sourceTexture, float2 uv, float splitPosition)
{
    uint width = 0;
    uint height = 0;
    sourceTexture.GetDimensions(width, height);
    float dividerHalfWidth = 1.5f / max(float(width), 1.0f);
    if (abs(uv.x - splitPosition) <= dividerHalfWidth)
    {
        return float3(1.0f, 0.9f, 0.2f);
    }

    return color;
}

float3 AdjustContrast(float3 color, float contrast)
{
    return (color - 0.5f.xxx) * contrast + 0.5f.xxx;
}

float3 AdjustSaturation(float3 color, float saturation)
{
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    return lerp(luminance.xxx, color, saturation);
}

float3 AdjustVibrance(float3 color, float vibrance)
{
    float maxComponent = max(max(color.r, color.g), color.b);
    float minComponent = min(min(color.r, color.g), color.b);
    float saturationLevel = maxComponent - minComponent;
    float vibranceAdjust = 1.0f + vibrance * (1.0f - saturationLevel);
    float luminance = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    return lerp(luminance.xxx, color, vibranceAdjust);
}

float3 ApplyColorGrading(float3 color, float contrast, float brightness, float saturation, float vibrance)
{
    color = max(color + brightness.xxx, 0.0f.xxx);
    color = AdjustContrast(color, contrast);
    color = AdjustSaturation(color, saturation);
    color = AdjustVibrance(color, vibrance);
    return saturate(color);
}

float3 ApplyVignette(float3 color, float2 uv, float strength, float radius)
{
    if (strength <= 0.0f)
    {
        return color;
    }

    float distanceFromCenter = length(uv - 0.5f.xx);
    float vignette = smoothstep(radius, radius - strength, distanceFromCenter);
    return color * vignette;
}

float3 AddFilmGrain(float3 color, float2 uv, float strength)
{
    if (strength <= 0.0f)
    {
        return color;
    }

    float noise = frac(sin(dot(uv, float2(12.9898f, 78.233f))) * 43758.5453123f);
    return saturate(color + (noise - 0.5f).xxx * strength);
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target0
{
    Texture2D<float4> sourceTexture = nonuniform(g_push.texture);
    SamplerState sourceSampler = nonuniform(g_push.sampler);
    float exposure = max(g_push.colorGrading.x, 0.0f);
    float gamma = max(g_push.colorGrading.y, 0.1f);
    float maxWhite = max(g_push.colorGrading.z, 0.1f);
    float debugSplit = saturate(g_push.colorGrading.w);
    float vibrance = g_push.postEffects0.x;
    float contrast = max(g_push.postEffects0.y, 0.0f);
    float brightness = g_push.postEffects0.z;
    float saturation = max(g_push.postEffects0.w, 0.0f);
    float vignetteStrength = g_push.postEffects1.x;
    float vignetteRadius = g_push.postEffects1.y;
    float filmGrainStrength = g_push.postEffects1.z;
    float chromaticAberration = g_push.postEffects1.w;
    uint toneMappingOperator = g_push.controls.x;
    uint debugView = g_push.controls.y;
    uint debugChannel = g_push.controls.z;
    bool antiAliasingEnabled = g_push.controls.w != 0u;

    float3 fullPostColor =
        antiAliasingEnabled
            ? ApplyFXAA(sourceTexture, sourceSampler, input.uv, exposure, chromaticAberration, toneMappingOperator,
                        maxWhite)
            : SampleToneMappedColor(sourceTexture, sourceSampler, input.uv, exposure, chromaticAberration,
                                    toneMappingOperator, maxWhite);
    fullPostColor = ApplyColorGrading(fullPostColor, contrast, brightness, saturation, vibrance);
    fullPostColor = ApplyVignette(fullPostColor, input.uv, vignetteStrength, vignetteRadius);
    fullPostColor = AddFilmGrain(fullPostColor, input.uv * 2048.0f, filmGrainStrength);

    float3 displayColor = fullPostColor;
    if (debugView == 1u)
    {
        float3 referenceColor =
            SampleToneMappedColor(sourceTexture, sourceSampler, input.uv, exposure, 0.0f, 2u, maxWhite);
        float3 selectedColor =
            SampleToneMappedColor(sourceTexture, sourceSampler, input.uv, exposure, 0.0f, toneMappingOperator, maxWhite);
        displayColor = (input.uv.x < debugSplit) ? referenceColor : selectedColor;
        displayColor = ApplySplitDivider(displayColor, sourceTexture, input.uv, debugSplit);
    }
    else if (debugView == 2u)
    {
        displayColor = ApplyChannelDebug(fullPostColor, debugChannel);
    }
    else if (debugView == 3u)
    {
        float3 toneMappedOnly =
            SampleToneMappedColor(sourceTexture, sourceSampler, input.uv, exposure, 0.0f, toneMappingOperator, maxWhite);
        displayColor = (input.uv.x < debugSplit) ? toneMappedOnly : fullPostColor;
        displayColor = ApplySplitDivider(displayColor, sourceTexture, input.uv, debugSplit);
    }

    float3 gammaCorrected = pow(saturate(max(displayColor, 0.0f.xxx)), 1.0f / gamma);
    return float4(gammaCorrected, 1.0f);
}
