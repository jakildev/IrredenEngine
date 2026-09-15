#include "ir_iso_common.metal"
#include "ir_sun_projection.metal"
#include "ir_sun_shadow_sample.metal"
#include <metal_atomic>

struct ShapeDescriptor {
    float4 worldPosition;
    float4 params;
    float4 rotation;
    uint shapeType;
    uint color;
    uint entityId;
    uint jointIndex;
    uint flags;
    uint lodLevel;
    uint _pad0;
    uint _pad1;
};

kernel void c_bake_box_sun_shadow(
    device const ShapeDescriptor* shapes [[buffer(20)]],
    device atomic_uint* sunDepthBuf [[buffer(28)]],
    constant int4& dispatch [[buffer(16)]],
    constant FrameDataSun& sunFrame [[buffer(29)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    const uint index = groupId.y * uint(dispatch.y) + groupId.x;
    if (index >= uint(dispatch.x)) return;
    const ShapeDescriptor shape = shapes[index];
    if (shape.shapeType != 0u || (shape.flags & 8u) == 0u || (shape.color >> 24u) == 0u) return;
    // BOX params count voxel centers; the SDF boundary adds half a raster cell.
    const float3 halfExtent = (shape.params.xyz - 1.0) * 0.5 + float3(0.5 / float(dispatch.z));
    if (any(halfExtent <= float3(0.0))) return;
    const float3 axisX = rotateByQuat(float3(1, 0, 0), shape.rotation);
    const float3 axisY = rotateByQuat(float3(0, 1, 0), shape.rotation);
    const float3 axisZ = rotateByQuat(float3(0, 0, 1), shape.rotation);
    const float3 center = sunSpaceProject(shape.worldPosition.xyz, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz);
    const float3 extent = abs(sunSpaceProject(axisX, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz)) * halfExtent.x
        + abs(sunSpaceProject(axisY, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz)) * halfExtent.y
        + abs(sunSpaceProject(axisZ, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz)) * halfExtent.z;
    const float3 direction = -float3(dot(sunFrame.sunDirection.xyz, axisX), dot(sunFrame.sunDirection.xyz, axisY), dot(sunFrame.sunDirection.xyz, axisZ));
    for (int cascade = 0; cascade < 2; ++cascade) {
        const float2 origin = cascade == 0 ? sunFrame.cascadeOriginUV_0 : sunFrame.cascadeOriginUV_1;
        const float2 texel = cascade == 0 ? sunFrame.cascadeTexelSize_0 : sunFrame.cascadeTexelSize_1;
        const int2 first = max(int2(ceil((center.xy - extent.xy - origin) / texel - 0.5)), int2(0));
        const int2 last = min(int2(floor((center.xy + extent.xy - origin) / texel - 0.5)), int2(kSunShadowMapDim - 1));
        if (any(first > last)) continue;
        const int2 size = last - first + 1;
        for (uint sampleIndex = localId.x; sampleIndex < uint(size.x * size.y); sampleIndex += 64u) {
            const int2 pixel = first + int2(int(sampleIndex) % size.x, int(sampleIndex) / size.x);
            const float2 uv = origin + (float2(pixel) + 0.5) * texel;
            const float3 worldDelta = sunFrame.sunBasisU.xyz * uv.x + sunFrame.sunBasisV.xyz * uv.y - shape.worldPosition.xyz;
            const float3 rayOrigin = float3(dot(worldDelta, axisX), dot(worldDelta, axisY), dot(worldDelta, axisZ));
            float nearDepth = -1e30;
            float farDepth = 1e30;
            bool hit = true;
            for (int axis = 0; axis < 3; ++axis) {
                if (abs(direction[axis]) < 1e-7) {
                    if (abs(rayOrigin[axis]) > halfExtent[axis]) hit = false;
                } else {
                    const float a = (-halfExtent[axis] - rayOrigin[axis]) / direction[axis];
                    const float b = (halfExtent[axis] - rayOrigin[axis]) / direction[axis];
                    nearDepth = max(nearDepth, min(a, b));
                    farDepth = min(farDepth, max(a, b));
                }
            }
            if (hit && nearDepth <= farDepth) {
                atomic_fetch_min_explicit(&sunDepthBuf[cascade * kCascadeTexelCount + pixel.y * kSunShadowMapDim + pixel.x], packSunSurfaceDepth(nearDepth), memory_order_relaxed);
            }
        }
    }
}
