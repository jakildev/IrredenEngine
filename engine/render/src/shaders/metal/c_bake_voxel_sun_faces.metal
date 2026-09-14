#include "ir_iso_common.metal"
#include "ir_sun_projection.metal"
#include "ir_sun_shadow_sample.metal"
#include <metal_atomic>

struct VoxelSunSample {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};
struct VoxelSunFaceFrame {
    float4 worldOrigin;
    float4 viewToWorld;
    int4 dispatch;
    int4 sourceMin;
    int4 sourceDims;
    float4 sourceAnchor;
};

inline void rasterSunFace(device atomic_uint* sunDepthBuf, float3 corner, float3 edgeU, float3 edgeV, float2 origin, float2 texelSize, int cascadeOffset) {
    const float2 a = edgeU.xy;
    const float2 b = edgeV.xy;
    const float determinant = a.x * b.y - a.y * b.x;
    if (abs(determinant) < 0.000001) return;
    const float2 uvMin = min(min(corner.xy, corner.xy + a), min(corner.xy + b, corner.xy + a + b));
    const float2 uvMax = max(max(corner.xy, corner.xy + a), max(corner.xy + b, corner.xy + a + b));
    const int2 first = max(int2(ceil((uvMin - origin) / texelSize - 0.5)), int2(0));
    const int2 last = min(int2(floor((uvMax - origin) / texelSize - 0.5)), int2(kSunShadowMapDim - 1));
    if (any(first > last)) return;
    for (int y = first.y; y <= last.y; ++y) {
        for (int x = first.x; x <= last.x; ++x) {
            const float2 delta = origin + (float2(x, y) + 0.5) * texelSize - corner.xy;
            const float2 faceUV = float2(delta.x * b.y - delta.y * b.x,
                                    a.x * delta.y - a.y * delta.x) / determinant;
            if (any(faceUV < float2(-0.00001)) || any(faceUV > float2(1.00001))) continue;
            const float depth = corner.z + faceUV.x * edgeU.z + faceUV.y * edgeV.z;
            atomic_fetch_min_explicit(&sunDepthBuf[cascadeOffset + y * kSunShadowMapDim + x], packSunDepth(depth, int2(0)), memory_order_relaxed);
        }
    }
}

kernel void c_bake_voxel_sun_faces(
    device const uint* sourceGrid [[buffer(9)]],
    device const uint* activeMask [[buffer(8)]],
    device const float4* positions [[buffer(5)]],
    device const VoxelSunSample* voxels [[buffer(6)]],
    constant VoxelSunFaceFrame& faceFrame [[buffer(16)]],
    device atomic_uint* sunDepthBuf [[buffer(28)]],
    constant FrameDataSun& sunFrame [[buffer(29)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    const uint index = (groupId.y * uint(faceFrame.dispatch.y) + groupId.x) * 64u + localId.x;

    if (index >= uint(faceFrame.dispatch.x)) return;
    const bool useSource = faceFrame.dispatch.w != 0;
    int3 sourceCell = int3(0);
    uint flags = 0u;
    float3 position;
    if (useSource) {
        if ((sourceGrid[index * 3u] >> 24u) == 0u) return;
        const int3 dims = faceFrame.sourceDims.xyz;
        sourceCell = int3(int(index) % dims.x, (int(index) / dims.x) % dims.y,
                         int(index) / (dims.x * dims.y));
        position = float3(sourceCell + faceFrame.sourceMin.xyz) + faceFrame.sourceAnchor.xyz;
    } else {
        if ((voxels[index].colorPacked >> 24u) == 0u) return;
        if ((activeMask[index >> 5u] & (1u << (index & 31u))) == 0u) return;
        flags = (voxels[index].materialFlagBone >> 8u) & 0xFFu;
        const float subdivisions = float(faceFrame.dispatch.z);
        position = float3(roundHalfUp(snapNearIntegerVoxelPosition(positions[index].xyz) * subdivisions)) / subdivisions;
    }
    for (int axis = 0; axis < 3; ++axis) {
        float3 normal = float3(0.0);
        normal[axis] = 1.0;
        const float3 worldAxis = rotateByQuat(normal, faceFrame.viewToWorld);
        const bool positive = dot(worldAxis, sunFrame.sunDirection.xyz) > 0.0;
        const int faceId = axis * 2 + (positive ? 1 : 0);
        if (useSource) {
            int3 neighbor = sourceCell;
            neighbor[axis] += positive ? 1 : -1;
            const int3 dims = faceFrame.sourceDims.xyz;
            if (all(neighbor >= int3(0)) && all(neighbor < dims)) {
                const int key = neighbor.x + dims.x * (neighbor.y + dims.y * neighbor.z);
                if ((sourceGrid[key * 3] >> 24u) != 0u) continue;
            }
        } else if (!faceIsExposed(flags, faceId)) {
            continue;
        }
        float3 corner = position - kVoxelRasterCellAnchor;
        corner[axis] += positive ? 1.0 : 0.0;
        float3 edgeU = float3(0.0);
        float3 edgeV = float3(0.0);
        edgeU[(axis + 1) % 3] = 1.0;
        edgeV[(axis + 2) % 3] = 1.0;
        corner = rotateByQuat(corner, faceFrame.viewToWorld) + faceFrame.worldOrigin.xyz;
        edgeU = rotateByQuat(edgeU, faceFrame.viewToWorld);
        edgeV = rotateByQuat(edgeV, faceFrame.viewToWorld);
        const float3 projected = sunSpaceProject(corner, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz);
        const float3 projectedU = sunSpaceProject(edgeU, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz);
        const float3 projectedV = sunSpaceProject(edgeV, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz);
        rasterSunFace(sunDepthBuf, projected, projectedU, projectedV, sunFrame.cascadeOriginUV_0, sunFrame.cascadeTexelSize_0, 0);
        rasterSunFace(sunDepthBuf, projected, projectedU, projectedV, sunFrame.cascadeOriginUV_1, sunFrame.cascadeTexelSize_1, kCascadeTexelCount);
    }
}
