// =============================================================================
// WestEngine - Shaders
// Shared frame constants for Phase 6 deferred rendering
// =============================================================================

struct FrameConstants
{
    row_major float4x4 viewProjection;
    float4 cameraPosition;
    float4 cameraForward;
    float4 cameraRight;
    float4 cameraUp;
    float4 cameraProjectionParams;
    float4 cameraFrustumParams;
    float4 lightDirection;
    float4 lightColor;
    float4 ambientColor;
    float4 skyZenithColor;
    float4 skyHorizonColor;
    float4 groundColor;
    float4 skyParams;
    float4 iblParams;
    row_major float4x4 lightViewProjection;
    float4 shadowParams;
    float4 renderFeatureFlags;
    float4 materialParams;
    float4 pbrParams;
    float4 ssaoParams;
};
