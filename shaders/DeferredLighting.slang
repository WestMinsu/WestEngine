// =============================================================================
// WestEngine - Shaders
// Full-screen deferred lighting pass with UE-style PBR terms
// =============================================================================

import Common.FrameConstants;
import Common.GlobalBindless;
import IBL;
import PBR;

struct DeferredLightingPushConstants
{
    DescriptorHandle<Texture2D<float4>> worldPositionTexture;
    DescriptorHandle<Texture2D<float4>> normalRoughnessTexture;
    DescriptorHandle<Texture2D<float4>> albedoMetallicTexture;
    DescriptorHandle<Texture2D<float>> shadowMapTexture;
    DescriptorHandle<Texture2D<float>> ambientOcclusionTexture;
    DescriptorHandle<TextureCube<float4>> prefilteredEnvironmentTexture;
    DescriptorHandle<TextureCube<float4>> irradianceEnvironmentTexture;
    DescriptorHandle<Texture2D<float4>> brdfLutTexture;
    DescriptorHandle<SamplerState> sceneSampler;
    DescriptorHandle<SamplerState> shadowSampler;
    DescriptorHandle<SamplerState> iblSampler;
    DescriptorHandle<StructuredBuffer<FrameConstants>> frameData;
};

[[vk::push_constant]]
ConstantBuffer<DeferredLightingPushConstants> g_push;

struct VSOutput
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

static const int kShadowPoissonSampleCount = 16;
static const float kShadowPoissonFilterRadiusTexels = 2.0f;
static const int kAoFilterSampleCount = 13;
static const float2 kShadowPoissonDisk[kShadowPoissonSampleCount] =
{
    float2(-0.94201624f, -0.39906216f),
    float2(0.94558609f, -0.76890725f),
    float2(-0.09418410f, -0.92938870f),
    float2(0.34495938f, 0.29387760f),
    float2(-0.91588581f, 0.45771432f),
    float2(-0.81544232f, -0.87912464f),
    float2(-0.38277543f, 0.27676845f),
    float2(0.97484398f, 0.75648379f),
    float2(0.44323325f, -0.97511554f),
    float2(0.53742981f, -0.47373420f),
    float2(-0.26496911f, -0.41893023f),
    float2(0.79197514f, 0.19090188f),
    float2(-0.24188840f, 0.99706507f),
    float2(-0.81409955f, 0.91437590f),
    float2(0.19984126f, 0.78641367f),
    float2(0.14383161f, -0.14100790f),
};
static const int2 kAoFilterOffsets[kAoFilterSampleCount] =
{
    int2(0, 0),
    int2(1, 0),
    int2(-1, 0),
    int2(0, 1),
    int2(0, -1),
    int2(1, 1),
    int2(-1, 1),
    int2(1, -1),
    int2(-1, -1),
    int2(2, 0),
    int2(-2, 0),
    int2(0, 2),
    int2(0, -2),
};
static const float kAoFilterWeights[kAoFilterSampleCount] =
{
    4.0f,
    2.0f,
    2.0f,
    2.0f,
    2.0f,
    1.0f,
    1.0f,
    1.0f,
    1.0f,
    0.75f,
    0.75f,
    0.75f,
    0.75f,
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

float ShadowRotationNoise(float2 seed, float depth)
{
    float hash = dot(seed, float2(12.9898f, 78.233f)) + depth * 37.719f;
    return frac(sin(hash) * 43758.5453123f) * 6.28318531f;
}

float2 RotatePoissonOffset(float2 offset, float sinTheta, float cosTheta)
{
    return float2(offset.x * cosTheta - offset.y * sinTheta, offset.x * sinTheta + offset.y * cosTheta);
}

float SampleDirectionalShadow(Texture2D<float> shadowMapTexture, SamplerState shadowSampler, float2 shadowUV,
                              float lightDepth, FrameConstants frame)
{
    float texelRadius = kShadowPoissonFilterRadiusTexels;
    float2 texelSize = frame.shadowParams.zw;
    float angle = ShadowRotationNoise(shadowUV * 4096.0f, lightDepth);
    float sinTheta = sin(angle);
    float cosTheta = cos(angle);
    float visibility = 0.0f;

    [unroll]
    for (int sampleIndex = 0; sampleIndex < kShadowPoissonSampleCount; ++sampleIndex)
    {
        float2 rotatedOffset = RotatePoissonOffset(kShadowPoissonDisk[sampleIndex], sinTheta, cosTheta);
        float2 sampleOffset = rotatedOffset * texelSize * texelRadius;
        float shadowDepth = shadowMapTexture.SampleLevel(shadowSampler, shadowUV + sampleOffset, 0.0f);
        visibility += (lightDepth - frame.shadowParams.x) <= shadowDepth ? 1.0f : 0.0f;
    }

    return visibility / float(kShadowPoissonSampleCount);
}

float ViewDepth(float3 worldPosition, FrameConstants frame)
{
    return dot(worldPosition - frame.cameraPosition.xyz, frame.cameraForward.xyz);
}

float SampleFilteredAmbientOcclusion(Texture2D<float> ambientOcclusionTexture,
                                     Texture2D<float4> worldPositionTexture,
                                     Texture2D<float4> normalRoughnessTexture,
                                     int2 centerPixel,
                                     float3 centerWorldPosition,
                                     float3 centerNormal,
                                     FrameConstants frame)
{
    uint width = 0;
    uint height = 0;
    ambientOcclusionTexture.GetDimensions(width, height);

    const int maxX = int(max(width, 1u)) - 1;
    const int maxY = int(max(height, 1u)) - 1;
    centerPixel.x = clamp(centerPixel.x, 0, maxX);
    centerPixel.y = clamp(centerPixel.y, 0, maxY);

    const float centerViewDepth = ViewDepth(centerWorldPosition, frame);
    const float filterWorldRadius = lerp(0.05f, 0.20f, saturate(centerViewDepth / 25.0f));

    float weightedOcclusion = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int sampleIndex = 0; sampleIndex < kAoFilterSampleCount; ++sampleIndex)
    {
        int2 samplePixel = centerPixel + kAoFilterOffsets[sampleIndex];
        samplePixel.x = clamp(samplePixel.x, 0, maxX);
        samplePixel.y = clamp(samplePixel.y, 0, maxY);

        float4 sampleWorldPosition = worldPositionTexture.Load(int3(samplePixel, 0));
        if (sampleWorldPosition.w < 0.5f)
        {
            continue;
        }

        float4 sampleNormalRoughness = normalRoughnessTexture.Load(int3(samplePixel, 0));
        float3 sampleNormal = normalize(sampleNormalRoughness.xyz * 2.0f - 1.0f.xxx);
        float normalWeight = smoothstep(0.78f, 0.98f, dot(centerNormal, sampleNormal));
        float distanceWeight = 1.0f - smoothstep(filterWorldRadius, filterWorldRadius * 3.0f,
                                                 length(sampleWorldPosition.xyz - centerWorldPosition));
        float sampleWeight = kAoFilterWeights[sampleIndex] * normalWeight * max(distanceWeight, 0.0f);
        if (sampleWeight <= 1e-4f)
        {
            continue;
        }

        weightedOcclusion += ambientOcclusionTexture.Load(int3(samplePixel, 0)) * sampleWeight;
        weightSum += sampleWeight;
    }

    if (weightSum <= 0.0f)
    {
        return ambientOcclusionTexture.Load(int3(centerPixel, 0));
    }

    return weightedOcclusion / weightSum;
}

[shader("fragment")]
float4 PSMain(VSOutput input) : SV_Target0
{
    Texture2D<float4> worldPositionTexture = nonuniform(g_push.worldPositionTexture);
    Texture2D<float4> normalRoughnessTexture = nonuniform(g_push.normalRoughnessTexture);
    Texture2D<float4> albedoMetallicTexture = nonuniform(g_push.albedoMetallicTexture);
    Texture2D<float> shadowMapTexture = nonuniform(g_push.shadowMapTexture);
    Texture2D<float> ambientOcclusionTexture = nonuniform(g_push.ambientOcclusionTexture);
    TextureCube<float4> prefilteredEnvironmentTexture = nonuniform(g_push.prefilteredEnvironmentTexture);
    TextureCube<float4> irradianceEnvironmentTexture = nonuniform(g_push.irradianceEnvironmentTexture);
    Texture2D<float4> brdfLutTexture = nonuniform(g_push.brdfLutTexture);
    SamplerState sceneSampler = nonuniform(g_push.sceneSampler);
    SamplerState shadowSampler = nonuniform(g_push.shadowSampler);
    SamplerState iblSampler = nonuniform(g_push.iblSampler);
    FrameConstants frame = nonuniform(g_push.frameData)[0];
    bool shadowsEnabled = frame.renderFeatureFlags.x > 0.5f;
    bool ssaoEnabled = frame.renderFeatureFlags.y > 0.5f;
    bool iblEnabled = frame.renderFeatureFlags.z > 0.5f;
    float globalDiffuseWeight = max(frame.materialParams.x, 0.0f);
    float globalSpecularWeight = max(frame.materialParams.y, 0.0f);
    float3 backgroundViewDir = ReconstructCameraRay(input.uv, frame);

    float4 worldPosition = worldPositionTexture.Sample(sceneSampler, input.uv);
    if (worldPosition.w < 0.5f)
    {
        return float4(SampleEnvironmentSky(prefilteredEnvironmentTexture, iblSampler, backgroundViewDir, frame), 1.0f);
    }

    float4 normalRoughness = normalRoughnessTexture.Sample(sceneSampler, input.uv);
    float4 albedoMetallic = albedoMetallicTexture.Sample(sceneSampler, input.uv);

    float3 normal = normalize(normalRoughness.xyz * 2.0f - 1.0f.xxx);
    float roughness = saturate(normalRoughness.w);
    float metallic = saturate(albedoMetallic.w);
    float3 albedo = saturate(albedoMetallic.rgb);
    float ambientOcclusion = ssaoEnabled
                                 ? SampleFilteredAmbientOcclusion(ambientOcclusionTexture, worldPositionTexture,
                                                                  normalRoughnessTexture, int2(input.position.xy),
                                                                  worldPosition.xyz, normal, frame)
                                 : 1.0f;
    ambientOcclusion = saturate(ambientOcclusion);

    float3 viewDir = normalize(frame.cameraPosition.xyz - worldPosition.xyz);
    float3 lightDir = normalize(-frame.lightDirection.xyz);
    float NdotL = saturate(dot(normal, lightDir));

    float3 shadowPosition = worldPosition.xyz + normal * frame.shadowParams.y;
    float4 lightClipPosition = mul(float4(shadowPosition, 1.0f), frame.lightViewProjection);
    float shadowVisibility = 1.0f;
    if (shadowsEnabled && lightClipPosition.w > 0.0f)
    {
        float3 lightNdc = lightClipPosition.xyz / lightClipPosition.w;
        float2 shadowUV = float2(lightNdc.x * 0.5f + 0.5f, -lightNdc.y * 0.5f + 0.5f);
        if (all(shadowUV >= 0.0f.xx) && all(shadowUV <= 1.0f.xx) && lightNdc.z >= 0.0f && lightNdc.z <= 1.0f)
        {
            shadowVisibility = SampleDirectionalShadow(shadowMapTexture, shadowSampler, shadowUV, lightNdc.z, frame);
        }
    }

    float3 fresnel = 0.0f.xxx;
    float3 specular = EvaluateCookTorrance(normal, viewDir, lightDir, albedo, roughness, metallic, fresnel);
    float3 diffuseWeight = (1.0f.xxx - fresnel) * (1.0f - metallic);
    float3 diffuse = diffuseWeight * albedo / 3.14159265f;

    float NdotV = saturate(dot(normal, viewDir));
    float3 f0 = lerp(0.04f.xxx, albedo, metallic);
    float3 reflectionDir = normalize(reflect(-viewDir, normal));
    float3 diffuseIbl = 0.0f.xxx;
    float3 specularIbl = 0.0f.xxx;
    if (iblEnabled)
    {
        float3 diffuseIrradiance = SampleEnvironmentDiffuse(irradianceEnvironmentTexture, iblSampler, normal);
        float3 specularRadiance = SampleEnvironmentSpecular(prefilteredEnvironmentTexture, iblSampler,
                                                            reflectionDir, roughness, frame);
        float3 iblFresnel =
            EvaluateIBLGGXFresnel(brdfLutTexture, iblSampler, roughness, NdotV, f0, frame.iblParams.y);
        float3 iblDiffuseWeight = (1.0f.xxx - iblFresnel) * (1.0f - metallic);
        diffuseIbl = diffuseIrradiance * albedo * iblDiffuseWeight * frame.ambientColor.rgb * frame.iblParams.x *
                     ambientOcclusion * globalDiffuseWeight;
        specularIbl = specularRadiance * iblFresnel * frame.ambientColor.rgb * globalSpecularWeight;
    }

    float3 directDiffuse =
        diffuse * frame.lightColor.rgb * NdotL * shadowVisibility * ambientOcclusion * globalDiffuseWeight;
    float3 directSpecular = specular * frame.lightColor.rgb * NdotL * shadowVisibility * globalSpecularWeight;
    float3 direct = directDiffuse + directSpecular;
    return float4(diffuseIbl + specularIbl + direct, 1.0f);
}
