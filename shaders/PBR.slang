// =============================================================================
// WestEngine - Shaders
// Cook-Torrance GGX helpers for Phase 6 deferred lighting
// =============================================================================

// Cook-Torrance specular BRDF:
// f_spec = D(h) * F(v, h) * G(l, v, h) / (4 * dot(n, l) * dot(n, v))
//
// D: GGX / Trowbridge-Reitz normal distribution function
// F: Schlick Fresnel approximation
// G: Smith-GGX height-correlated visibility term

float DistributionGGX(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float d = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / max(3.14159265f * d * d, 1e-4f);
}

float VisibilitySmithGGXCorrelated(float NdotV, float NdotL, float alpha)
{
    float a2 = alpha * alpha;
    float gv = NdotL * sqrt(max(NdotV * (NdotV - NdotV * a2) + a2, 1e-4f));
    float gl = NdotV * sqrt(max(NdotL * (NdotL - NdotL * a2) + a2, 1e-4f));
    return 0.5f / max(gv + gl, 1e-4f);
}

float3 FresnelSchlick(float cosTheta, float3 f0)
{
    return f0 + (1.0f.xxx - f0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

float3 EvaluateCookTorrance(float3 normal, float3 viewDir, float3 lightDir, float3 baseColor, float roughness,
                            float metallic, out float3 fresnelOut)
{
    float3 halfDir = normalize(viewDir + lightDir);

    float NdotL = saturate(dot(normal, lightDir));
    float NdotV = saturate(dot(normal, viewDir));
    float NdotH = saturate(dot(normal, halfDir));
    float VdotH = saturate(dot(viewDir, halfDir));

    float alpha = max(roughness * roughness, 0.045f);
    float3 f0 = lerp(0.04f.xxx, baseColor, metallic);
    float3 F = FresnelSchlick(VdotH, f0);
    float D = DistributionGGX(NdotH, alpha);
    float G = VisibilitySmithGGXCorrelated(NdotV, NdotL, alpha);

    fresnelOut = F;
    return D * G * F;
}
