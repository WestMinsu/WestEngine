// =============================================================================
// WestEngine - Shaders
// Full-screen SSAO pass reading depth and outputting ambient occlusion
// =============================================================================

import Common.FrameConstants;
import Common.GlobalBindless;

struct SSAOPushConstants
{
    DescriptorHandle<Texture2D<float>> sceneDepthTexture;
    DescriptorHandle<Texture2D<float4>> normalRoughnessTexture;
    DescriptorHandle<StructuredBuffer<FrameConstants>> frameData;
    uint padding0;
    uint padding1;
};

[[vk::push_constant]]
ConstantBuffer<SSAOPushConstants> g_push;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

static const int kSsaoMaxSampleCount = 64;

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

float HashNoise(float2 seed)
{
    return frac(sin(dot(seed, float2(12.9898f, 78.233f))) * 43758.5453123f);
}

float3 GenerateSampleKernel(int index, int sampleCount)
{
    float normalizedIndex = (float(index) + 0.5f) / float(max(sampleCount, 1));
    float angle = normalizedIndex * 6.28318531f * 2.39996323f;
    float z = lerp(0.1f, 1.0f, normalizedIndex * normalizedIndex);
    float radial = sqrt(saturate(1.0f - z * z));

    return float3(cos(angle) * radial, sin(angle) * radial, z);
}

float3 WorldToView(float3 worldPosition, FrameConstants frame)
{
    float3 relative = worldPosition - frame.cameraPosition.xyz;
    return float3(dot(relative, frame.cameraRight.xyz),
                  dot(relative, frame.cameraUp.xyz),
                  dot(relative, frame.cameraForward.xyz));
}

float LinearizePositiveViewDepth(float depth, FrameConstants frame)
{
    float nearPlane = frame.cameraProjectionParams.z;
    float farPlane = frame.cameraProjectionParams.w;
    float zScale = farPlane / (nearPlane - farPlane);
    float zTranslate = (nearPlane * farPlane) / (nearPlane - farPlane);
    float denominator = min(depth + zScale, -1e-5f);
    float viewZ = -zTranslate / denominator;
    return max(-viewZ, nearPlane);
}

float3 ReconstructWorldPosition(float2 uv, float depth, FrameConstants frame)
{
    float viewDepth = LinearizePositiveViewDepth(depth, frame);
    float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
    float tanHalfFov = frame.cameraProjectionParams.x;
    float aspectRatio = frame.cameraProjectionParams.y;
    float viewX = ndc.x * tanHalfFov * aspectRatio * viewDepth;
    float viewY = ndc.y * tanHalfFov * viewDepth;

    return frame.cameraPosition.xyz +
           frame.cameraRight.xyz * viewX +
           frame.cameraUp.xyz * viewY +
           frame.cameraForward.xyz * viewDepth;
}

float LoadDepth(Texture2D<float> sceneDepthTexture, int2 pixelCoord, int maxX, int maxY)
{
    pixelCoord.x = clamp(pixelCoord.x, 0, maxX);
    pixelCoord.y = clamp(pixelCoord.y, 0, maxY);
    return sceneDepthTexture.Load(int3(pixelCoord, 0));
}

float3 ReconstructWorldNormal(Texture2D<float> sceneDepthTexture, Texture2D<float4> normalRoughnessTexture,
                              int2 pixelCoord, uint width, uint height, FrameConstants frame)
{
    int maxX = int(max(width, 1u)) - 1;
    int maxY = int(max(height, 1u)) - 1;
    float2 dimensions = float2(float(max(width, 1u)), float(max(height, 1u)));
    float2 invDimensions = 1.0f.xx / dimensions;
    float2 centerUv = (float2(pixelCoord) + 0.5f.xx) * invDimensions;

    float centerDepth = LoadDepth(sceneDepthTexture, pixelCoord, maxX, maxY);
    float leftDepth = LoadDepth(sceneDepthTexture, pixelCoord + int2(-1, 0), maxX, maxY);
    float rightDepth = LoadDepth(sceneDepthTexture, pixelCoord + int2(1, 0), maxX, maxY);
    float upDepth = LoadDepth(sceneDepthTexture, pixelCoord + int2(0, -1), maxX, maxY);
    float downDepth = LoadDepth(sceneDepthTexture, pixelCoord + int2(0, 1), maxX, maxY);

    float3 centerPosition = ReconstructWorldPosition(centerUv, centerDepth, frame);
    float3 leftPosition = ReconstructWorldPosition(centerUv + float2(-invDimensions.x, 0.0f), leftDepth, frame);
    float3 rightPosition = ReconstructWorldPosition(centerUv + float2(invDimensions.x, 0.0f), rightDepth, frame);
    float3 upPosition = ReconstructWorldPosition(centerUv + float2(0.0f, -invDimensions.y), upDepth, frame);
    float3 downPosition = ReconstructWorldPosition(centerUv + float2(0.0f, invDimensions.y), downDepth, frame);

    float centerViewDepth = WorldToView(centerPosition, frame).z;
    float leftViewDepth = WorldToView(leftPosition, frame).z;
    float rightViewDepth = WorldToView(rightPosition, frame).z;
    float upViewDepth = WorldToView(upPosition, frame).z;
    float downViewDepth = WorldToView(downPosition, frame).z;

    float3 dx = abs(rightViewDepth - centerViewDepth) < abs(leftViewDepth - centerViewDepth)
                    ? rightPosition - centerPosition
                    : centerPosition - leftPosition;
    float3 dy = abs(downViewDepth - centerViewDepth) < abs(upViewDepth - centerViewDepth)
                    ? downPosition - centerPosition
                    : centerPosition - upPosition;

    float3 gbufferNormal = normalize(normalRoughnessTexture.Load(int3(pixelCoord, 0)).xyz * 2.0f - 1.0f.xxx);
    float3 normal = cross(dx, dy);
    float normalLengthSq = dot(normal, normal);
    if (normalLengthSq <= 1e-8f)
    {
        return gbufferNormal;
    }

    normal *= rsqrt(normalLengthSq);
    return dot(normal, gbufferNormal) < 0.0f ? -normal : normal;
}

[shader("fragment")]
float PSMain(VSOutput input) : SV_Target0
{
    Texture2D<float> sceneDepthTexture = nonuniform(g_push.sceneDepthTexture);
    Texture2D<float4> normalRoughnessTexture = nonuniform(g_push.normalRoughnessTexture);
    FrameConstants frame = nonuniform(g_push.frameData)[0];

    float ssaoRadius = max(frame.ssaoParams.x, 0.0f);
    float ssaoBias = max(frame.ssaoParams.y, 0.0f);
    int ssaoSampleCount = min(max(int(frame.ssaoParams.z + 0.5f), 0), kSsaoMaxSampleCount);
    float ssaoPower = max(frame.ssaoParams.w, 0.5f);
    if (ssaoRadius <= 0.0f || ssaoSampleCount <= 0)
    {
        return 1.0f;
    }

    uint width = 0;
    uint height = 0;
    sceneDepthTexture.GetDimensions(width, height);

    int2 pixelCoord = int2(input.position.xy);
    pixelCoord.x = clamp(pixelCoord.x, 0, int(max(width, 1u)) - 1);
    pixelCoord.y = clamp(pixelCoord.y, 0, int(max(height, 1u)) - 1);

    float2 dimensions = float2(float(max(width, 1u)), float(max(height, 1u)));
    float2 pixelUv = (float2(pixelCoord) + 0.5f.xx) / dimensions;
    float centerDepth = sceneDepthTexture.Load(int3(pixelCoord, 0));
    if (centerDepth >= 0.9999f)
    {
        return 1.0f;
    }

    float3 worldPosition = ReconstructWorldPosition(pixelUv, centerDepth, frame);
    float3 normal = ReconstructWorldNormal(sceneDepthTexture, normalRoughnessTexture, pixelCoord, width, height, frame);
    float3 tangentBase =
        normalize(cross(abs(normal.y) > 0.9f ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, 1.0f, 0.0f), normal));
    float3 bitangentBase = normalize(cross(normal, tangentBase));
    float tangentAngle = HashNoise(float2(pixelCoord)) * 6.28318531f;
    float3 tangent = tangentBase * cos(tangentAngle) + bitangentBase * sin(tangentAngle);
    float3 bitangent = normalize(cross(normal, tangent));

    float3 currentViewPosition = WorldToView(worldPosition, frame);
    float occlusion = 0.0f;
    float sampleWeightSum = 0.0f;

    [loop]
    for (int sampleIndex = 0; sampleIndex < kSsaoMaxSampleCount; ++sampleIndex)
    {
        if (sampleIndex >= ssaoSampleCount)
        {
            break;
        }

        float3 kernelSample = GenerateSampleKernel(sampleIndex, ssaoSampleCount);
        float3 sampleDirection =
            tangent * kernelSample.x +
            bitangent * kernelSample.y +
            normal * kernelSample.z;
        float normalizedIndex = (float(sampleIndex) + 0.5f) / float(ssaoSampleCount);
        float sampleScale = lerp(0.1f, 1.0f, normalizedIndex * normalizedIndex);
        float3 sampleWorldPosition = worldPosition + sampleDirection * ssaoRadius * sampleScale;
        float4 clipPosition = mul(float4(sampleWorldPosition, 1.0f), frame.viewProjection);
        if (clipPosition.w <= 0.0f)
        {
            continue;
        }

        float2 sampleUv = float2(clipPosition.x / clipPosition.w * 0.5f + 0.5f,
                                 -clipPosition.y / clipPosition.w * 0.5f + 0.5f);
        if (any(sampleUv < 0.0f.xx) || any(sampleUv > 1.0f.xx))
        {
            continue;
        }

        int2 samplePixel = int2(sampleUv * dimensions);
        samplePixel.x = clamp(samplePixel.x, 0, int(max(width, 1u)) - 1);
        samplePixel.y = clamp(samplePixel.y, 0, int(max(height, 1u)) - 1);

        float sampleDepth = sceneDepthTexture.Load(int3(samplePixel, 0));
        if (sampleDepth >= 0.9999f)
        {
            continue;
        }

        float2 samplePixelUv = (float2(samplePixel) + 0.5f.xx) / dimensions;
        float3 sampledWorldPosition = ReconstructWorldPosition(samplePixelUv, sampleDepth, frame);
        float3 worldOffset = sampledWorldPosition - worldPosition;
        float worldDistance = length(worldOffset);
        if (worldDistance <= 1e-4f)
        {
            continue;
        }

        float3 worldSampleDirection = worldOffset / worldDistance;
        float horizonWeight = smoothstep(0.05f, 0.35f, dot(normal, worldSampleDirection));
        float distanceWeight = 1.0f - smoothstep(ssaoRadius, ssaoRadius * 3.0f, worldDistance);
        float3 sampledViewPosition = WorldToView(sampledWorldPosition, frame);
        float3 targetViewPosition = WorldToView(sampleWorldPosition, frame);
        float depthDelta = abs(currentViewPosition.z - sampledViewPosition.z);
        float rangeWeight = smoothstep(0.0f, 1.0f, ssaoRadius / max(depthDelta, 1e-4f));
        float sampleWeight = horizonWeight * distanceWeight * rangeWeight;
        if (sampleWeight <= 1e-4f)
        {
            continue;
        }

        if (sampledViewPosition.z <= targetViewPosition.z - ssaoBias)
        {
            occlusion += sampleWeight;
        }

        sampleWeightSum += sampleWeight;
    }

    if (sampleWeightSum <= 0.0f)
    {
        return 1.0f;
    }

    float ambientOcclusion = 1.0f - (occlusion / sampleWeightSum);
    return pow(saturate(ambientOcclusion), ssaoPower);
}
