// =============================================================================
// WestEngine - Shaders
// Full-screen Bokeh depth of field pass reading HDR scene color and G-Buffer
// =============================================================================

import Common.FrameConstants;
import Common.GlobalBindless;

struct BokehDOFPushConstants
{
    DescriptorHandle<Texture2D<float4>> sceneColorTexture;
    DescriptorHandle<Texture2D<float4>> worldPositionTexture;
    DescriptorHandle<StructuredBuffer<FrameConstants>> frameData;
    DescriptorHandle<SamplerState> sampler;
    float4 dofParams0;
    float4 dofParams1;
};

[[vk::push_constant]]
ConstantBuffer<BokehDOFPushConstants> g_push;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

static const int kBokehSampleCount = 19;
static const float2 kHexOffsets[kBokehSampleCount] =
{
    float2(0.0f, 0.0f),
    float2(1.0f, 0.0f),
    float2(0.5f, 0.8660254f),
    float2(-0.5f, 0.8660254f),
    float2(-1.0f, 0.0f),
    float2(-0.5f, -0.8660254f),
    float2(0.5f, -0.8660254f),
    float2(2.0f, 0.0f),
    float2(1.5f, 0.8660254f),
    float2(1.0f, 1.7320508f),
    float2(0.0f, 2.0f),
    float2(-1.0f, 1.7320508f),
    float2(-1.5f, 0.8660254f),
    float2(-2.0f, 0.0f),
    float2(-1.5f, -0.8660254f),
    float2(-1.0f, -1.7320508f),
    float2(0.0f, -2.0f),
    float2(1.0f, -1.7320508f),
    float2(1.5f, -0.8660254f),
};

[shader("vertex")]
VSOutput VSMain(uint vertexID : SV_VertexID)
{
    float2 positions[3] =
    {
        float2(-1.0f, -1.0f),
        float2(3.0f, -1.0f),
        float2(-1.0f, 3.0f),
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

float ComputeViewDepth(float3 worldPosition, FrameConstants frame)
{
    return dot(worldPosition - frame.cameraPosition.xyz, frame.cameraForward.xyz);
}

float ComputeCoC(float viewDepth, float focusDistance, float focusRange, float maxBlurRadius)
{
    float depthDelta = viewDepth - focusDistance;
    float normalized = abs(depthDelta) / max(focusRange, 1e-3f);
    if (depthDelta < 0.0f)
    {
        normalized *= 1.15f;
    }

    float blurAmount = 1.0f - exp(-normalized * 1.35f);
    return saturate(blurAmount) * maxBlurRadius;
}

bool LoadWorldPosition(Texture2D<float4> worldPositionTexture, float2 uv, uint width, uint height, out float4 worldPosition)
{
    int2 pixelCoord = int2(uv * float2(width, height));
    pixelCoord.x = clamp(pixelCoord.x, 0, int(max(width, 1u)) - 1);
    pixelCoord.y = clamp(pixelCoord.y, 0, int(max(height, 1u)) - 1);
    worldPosition = worldPositionTexture.Load(int3(pixelCoord, 0));
    return worldPosition.w >= 0.5f;
}

int DetermineSampleCount(float blurRadius, float maxBlurRadius)
{
    if (blurRadius > maxBlurRadius * 0.7f)
    {
        return 19;
    }

    if (blurRadius > maxBlurRadius * 0.35f)
    {
        return 13;
    }

    return 7;
}

float3 ApplyBokeh(Texture2D<float4> sceneColorTexture, Texture2D<float4> worldPositionTexture, SamplerState colorSampler,
                  FrameConstants frame, float2 uv, float focusRangeScale, float maxBlurRadius, float intensity,
                  float highlightBoost, float foregroundBias)
{
    uint width = 0;
    uint height = 0;
    sceneColorTexture.GetDimensions(width, height);
    float2 texelSize = 1.0f / float2(max(width, 1u), max(height, 1u));
    float2 safeUvMin = texelSize * 0.5f;
    float2 safeUvMax = 1.0f.xx - safeUvMin;

    float3 centerColor = sceneColorTexture.SampleLevel(colorSampler, clamp(uv, safeUvMin, safeUvMax), 0.0f).rgb;
    if (intensity <= 0.0f)
    {
        return centerColor;
    }

    float4 centerWorldPosition;
    if (!LoadWorldPosition(worldPositionTexture, uv, width, height, centerWorldPosition))
    {
        return centerColor;
    }

    float centerDepth = ComputeViewDepth(centerWorldPosition.xyz, frame);
    float4 focusWorldPosition;
    float focusDistance = centerDepth;
    if (LoadWorldPosition(worldPositionTexture, 0.5f.xx, width, height, focusWorldPosition))
    {
        focusDistance = ComputeViewDepth(focusWorldPosition.xyz, frame);
    }

    float focusRange = max(1.5f, focusDistance * focusRangeScale);
    float centerCoC = ComputeCoC(centerDepth, focusDistance, focusRange, maxBlurRadius);
    if (centerCoC < 0.35f)
    {
        return centerColor;
    }

    float3 blurredColor = 0.0f.xxx;
    float totalWeight = 0.0f;
    int sampleCount = DetermineSampleCount(centerCoC, maxBlurRadius);
    float sampleRadius = centerCoC * 0.5f;

    [unroll]
    for (int sampleIndex = 0; sampleIndex < kBokehSampleCount; ++sampleIndex)
    {
        if (sampleIndex >= sampleCount)
        {
            break;
        }

        float2 sampleUv = clamp(uv + (kHexOffsets[sampleIndex] * sampleRadius * texelSize), safeUvMin, safeUvMax);
        float3 sampleColor = sceneColorTexture.SampleLevel(colorSampler, sampleUv, 0.0f).rgb;

        float4 sampleWorldPosition;
        float sampleDepth = centerDepth;
        float sampleCoC = centerCoC;
        bool sampleValid = LoadWorldPosition(worldPositionTexture, sampleUv, width, height, sampleWorldPosition);
        if (sampleValid)
        {
            sampleDepth = ComputeViewDepth(sampleWorldPosition.xyz, frame);
            sampleCoC = ComputeCoC(sampleDepth, focusDistance, focusRange, maxBlurRadius);
        }

        float radialWeight = 1.0f / (1.0f + 0.18f * length(kHexOffsets[sampleIndex]));
        float coverage = smoothstep(0.0f, max(centerCoC, 1e-3f), max(sampleCoC, centerCoC * foregroundBias));
        float depthWeight = 1.0f;
        if (sampleValid && sampleDepth < centerDepth)
        {
            depthWeight = saturate(sampleCoC / max(centerCoC, 1e-3f));
        }

        float luminance = dot(sampleColor, float3(0.299f, 0.587f, 0.114f));
        float highlightWeight = 1.0f + smoothstep(1.0f, 4.0f, luminance) * highlightBoost;
        float weight = max(radialWeight * coverage * depthWeight * highlightWeight, 1e-3f);
        blurredColor += sampleColor * weight;
        totalWeight += weight;
    }

    if (totalWeight <= 0.0f)
    {
        return centerColor;
    }

    blurredColor /= totalWeight;
    float blendFactor = saturate(centerCoC / max(maxBlurRadius, 1e-3f)) * intensity;
    return lerp(centerColor, blurredColor, blendFactor);
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target0
{
    Texture2D<float4> sceneColorTexture = nonuniform(g_push.sceneColorTexture);
    Texture2D<float4> worldPositionTexture = nonuniform(g_push.worldPositionTexture);
    SamplerState colorSampler = nonuniform(g_push.sampler);
    FrameConstants frame = nonuniform(g_push.frameData)[0];

    float focusRangeScale = max(g_push.dofParams0.x, 0.0f);
    float maxBlurRadius = max(g_push.dofParams0.y, 0.0f);
    float intensity = saturate(g_push.dofParams0.z);
    float highlightBoost = max(g_push.dofParams0.w, 0.0f);
    float foregroundBias = saturate(g_push.dofParams1.x);

    float3 blurredColor =
        ApplyBokeh(sceneColorTexture, worldPositionTexture, colorSampler, frame, input.uv, focusRangeScale,
                   maxBlurRadius, intensity, highlightBoost, foregroundBias);
    return float4(max(blurredColor, 0.0f.xxx), 1.0f);
}
