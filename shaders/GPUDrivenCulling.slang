// =============================================================================
// WestEngine - Shaders
// Compute frustum culling that emits indexed indirect draw arguments
// =============================================================================

import Common.FrameConstants;
import Common.GlobalBindless;

struct SceneDrawRecord
{
    uint materialIndex;
    uint indexCount;
    uint firstIndex;
    int vertexOffset;
    row_major float4x4 model;
    float4 boundsSphere;
    float4 boundsMin;
    float4 boundsMax;
};

struct DrawIndexedIndirectArgs
{
    uint indexCountPerInstance;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
};

struct GPUDrivenCullingPushConstants
{
    DescriptorHandle<StructuredBuffer<FrameConstants>> frameData;
    DescriptorHandle<StructuredBuffer<SceneDrawRecord>> drawData;
    DescriptorHandle<RWStructuredBuffer<DrawIndexedIndirectArgs>> indirectArgs;
    DescriptorHandle<RWStructuredBuffer<uint>> indirectCount;
    uint drawCount;
    uint padding0;
    uint padding1;
    uint padding2;
};

[[vk::push_constant]]
ConstantBuffer<GPUDrivenCullingPushConstants> g_push;

bool IsAabbInsidePlane(float3 boundsMin, float3 boundsMax, float3 normal, float distance)
{
    float3 positiveVertex = boundsMin;
    positiveVertex.x = normal.x >= 0.0f ? boundsMax.x : boundsMin.x;
    positiveVertex.y = normal.y >= 0.0f ? boundsMax.y : boundsMin.y;
    positiveVertex.z = normal.z >= 0.0f ? boundsMax.z : boundsMin.z;
    return dot(normal, positiveVertex) + distance >= 0.0f;
}

bool IsVisibleAabb(float3 boundsMin, float3 boundsMax, FrameConstants frame)
{
    float nearPlane = frame.cameraProjectionParams.z;
    float farPlane = frame.cameraProjectionParams.w;
    float tanY = frame.cameraProjectionParams.x;
    float tanX = tanY * frame.cameraProjectionParams.y;

    float3 cameraPosition = frame.cameraPosition.xyz;
    float3 cameraForward = frame.cameraForward.xyz;
    float3 cameraRight = frame.cameraRight.xyz;
    float3 cameraUp = frame.cameraUp.xyz;

    float3 nearNormal = cameraForward;
    float nearDistance = -dot(nearNormal, cameraPosition + cameraForward * nearPlane);
    if (!IsAabbInsidePlane(boundsMin, boundsMax, nearNormal, nearDistance))
    {
        return false;
    }

    float3 farNormal = -cameraForward;
    float farDistance = -dot(farNormal, cameraPosition + cameraForward * farPlane);
    if (!IsAabbInsidePlane(boundsMin, boundsMax, farNormal, farDistance))
    {
        return false;
    }

    float3 leftNormal = cameraRight + cameraForward * tanX;
    if (!IsAabbInsidePlane(boundsMin, boundsMax, leftNormal, -dot(leftNormal, cameraPosition)))
    {
        return false;
    }

    float3 rightNormal = -cameraRight + cameraForward * tanX;
    if (!IsAabbInsidePlane(boundsMin, boundsMax, rightNormal, -dot(rightNormal, cameraPosition)))
    {
        return false;
    }

    float3 bottomNormal = cameraUp + cameraForward * tanY;
    if (!IsAabbInsidePlane(boundsMin, boundsMax, bottomNormal, -dot(bottomNormal, cameraPosition)))
    {
        return false;
    }

    float3 topNormal = -cameraUp + cameraForward * tanY;
    return IsAabbInsidePlane(boundsMin, boundsMax, topNormal, -dot(topNormal, cameraPosition));
}

[numthreads(64, 1, 1)]
[shader("compute")]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint drawIndex = dispatchThreadId.x;
    if (drawIndex >= g_push.drawCount)
    {
        return;
    }

    FrameConstants frame = nonuniform(g_push.frameData)[0];
    SceneDrawRecord draw = nonuniform(g_push.drawData)[drawIndex];
    if (!IsVisibleAabb(draw.boundsMin.xyz, draw.boundsMax.xyz, frame))
    {
        return;
    }

    uint visibleIndex = 0;
    RWStructuredBuffer<uint> indirectCount = nonuniform(g_push.indirectCount);
    InterlockedAdd(indirectCount[0], 1u, visibleIndex);

    DrawIndexedIndirectArgs args;
    args.indexCountPerInstance = draw.indexCount;
    args.instanceCount = 1;
    args.firstIndex = draw.firstIndex;
    args.vertexOffset = draw.vertexOffset;
    args.firstInstance = drawIndex;
    nonuniform(g_push.indirectArgs)[visibleIndex] = args;
}
