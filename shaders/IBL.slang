// =============================================================================
// WestEngine - Shaders
// Asset-driven sky and IBL helpers for Phase 6
// =============================================================================

import Common.FrameConstants;

float3 ReconstructCameraRay(float2 uv, FrameConstants frame)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, (1.0f - uv.y) * 2.0f - 1.0f);
    float tanHalfFov = frame.cameraProjectionParams.x;
    float aspectRatio = frame.cameraProjectionParams.y;

    float3 ray =
        frame.cameraForward.xyz +
        frame.cameraRight.xyz * (ndc.x * tanHalfFov * aspectRatio) +
        frame.cameraUp.xyz * (ndc.y * tanHalfFov);
    return normalize(ray);
}

float3 SampleEnvironmentSky(TextureCube<float4> prefilteredEnvironment, SamplerState iblSampler, float3 direction,
                            FrameConstants frame)
{
    return prefilteredEnvironment.SampleLevel(iblSampler, normalize(direction), 0.0f).rgb *
           frame.ambientColor.rgb * frame.iblParams.z;
}

float3 SampleEnvironmentDiffuse(TextureCube<float4> irradianceEnvironment, SamplerState iblSampler, float3 normal)
{
    return irradianceEnvironment.SampleLevel(iblSampler, normalize(normal), 0.0f).rgb;
}

float3 FresnelSchlickRoughness(float cosTheta, float3 f0, float roughness)
{
    float3 oneMinusRoughness = (1.0f - roughness).xxx;
    return f0 + (max(oneMinusRoughness, f0) - f0) * pow(1.0f - cosTheta, 5.0f);
}

float3 SampleEnvironmentSpecular(TextureCube<float4> prefilteredEnvironment, SamplerState iblSampler,
                                 float3 reflectionDirection, float roughness, FrameConstants frame)
{
    float mipLevel = saturate(roughness) * frame.iblParams.w;
    return prefilteredEnvironment.SampleLevel(iblSampler, normalize(reflectionDirection), mipLevel).rgb;
}

float2 SampleEnvironmentBRDF(Texture2D<float4> brdfLut, SamplerState iblSampler, float roughness, float NdotV)
{
    float2 brdfSamplePoint = saturate(float2(NdotV, roughness));
    return brdfLut.SampleLevel(iblSampler, brdfSamplePoint, 0.0f).rg;
}

// Fdez-Aguera multiple-scattering compensation for split-sum IBL.
float3 EvaluateIBLGGXFresnel(Texture2D<float4> brdfLut, SamplerState iblSampler, float roughness, float NdotV,
                             float3 f0, float specularWeight)
{
    float2 envBrdf = SampleEnvironmentBRDF(brdfLut, iblSampler, roughness, NdotV);
    float3 roughnessFresnel = max((1.0f - roughness).xxx, f0) - f0;
    float3 kS = f0 + roughnessFresnel * pow(saturate(1.0f - NdotV), 5.0f);
    float3 FssEss = specularWeight * (kS * envBrdf.x + envBrdf.y);

    float Ems = 1.0f - (envBrdf.x + envBrdf.y);
    float3 Favg = specularWeight * (f0 + (1.0f.xxx - f0) / 21.0f);
    float3 FmsEms = Ems * FssEss * Favg / max(1.0f.xxx - Favg * Ems, 1e-4f.xxx);

    return saturate(FssEss + FmsEms);
}
