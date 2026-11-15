// Rasterizes the exposed sun-facing faces of the cells a canvas rasterizes
// (buffers 5/6/8: the same positions, colors and active mask stage 1 reads)
// into the sun depth map. A revoxelized canvas therefore casts its resampled
// lattice, the geometry its receiver reads and its display shows.
#include "ir_iso_common.metal"
#include "ir_fog_shadow_caster.metal"
#include "ir_sun_projection.metal"
#include "ir_sun_face_query_layout.metal"
#include "ir_sun_shadow_sample.metal"
#include <metal_atomic>

struct VoxelSunSample {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};
struct VoxelSunFaceFrame {
    float4 worldOrigin; // xyz: world translation; w: per-axis raster enabled
    float4 viewToWorld;
    int4 dispatch;
    int4 admission;
};

inline void rasterSunFace(device atomic_uint* sunDepthBuf, float3 corner, float3 edgeU, float3 edgeV, float2 origin, float2 texelSize, int cascadeOffset, uint faceMarker) {
    const float2 a = edgeU.xy;
    const float2 b = edgeV.xy;
    const float determinant = projectedFaceDeterminant(a, b);
    if (abs(determinant) < 0.000001) return;
    const float2 uvMin = min(min(corner.xy, corner.xy + a), min(corner.xy + b, corner.xy + a + b));
    const float2 uvMax = max(max(corner.xy, corner.xy + a), max(corner.xy + b, corner.xy + a + b));
    const int2 first = max(int2(ceil((uvMin - origin) / texelSize - 0.5)), int2(0));
    const int2 last = min(int2(floor((uvMax - origin) / texelSize - 0.5)), int2(kSunShadowMapDim - 1));
    if (any(first > last)) return;
    for (int y = first.y; y <= last.y; ++y) {
        for (int x = first.x; x <= last.x; ++x) {
            const float2 delta = origin + (float2(x, y) + 0.5) * texelSize - corner.xy;
            const float2 faceUV = projectedFaceCoordinates(delta, a, b, determinant);
            if (any(faceUV < float2(-0.00001)) || any(faceUV > float2(1.00001))) continue;
            const float depth = corner.z + faceUV.x * edgeU.z + faceUV.y * edgeV.z;
            atomic_fetch_min_explicit(&sunDepthBuf[cascadeOffset + y * kSunShadowMapDim + x], (packSunDepth(depth, int2(0)) | faceMarker), memory_order_relaxed);
        }
    }
}

#include "ir_sun_face_index.metal"

kernel void c_bake_voxel_sun_faces(
    device const uint* activeMask [[buffer(8)]],
    device const float4* positions [[buffer(5)]],
    device const VoxelSunSample* voxels [[buffer(6)]],
    device uint* admissionScratch [[buffer(26)]],
    constant VoxelSunFaceFrame& faceFrame [[buffer(16)]],
    device atomic_uint* sunDepthBuf [[buffer(28)]],
    constant FrameDataSun& sunFrame [[buffer(29)]],
    constant FogShadowObserverData& fogObservers [[buffer(27)]],
    texture2d<uint, access::read> fogShadowWindow [[texture(4)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    threadgroup uint sharedRecordCounts[64];
    const uint lane = localId.x;
    if (faceFrame.admission.x == 1) {
        if (lane == 0u) {
            uint total = 0u;
            for (uint group = 0u; group < uint(faceFrame.admission.y); ++group) {
                const uint count = admissionScratch[group];
                admissionScratch[group] = total;
                total += count;
            }
            const uint capacity = atomic_load_explicit(
                &sunDepthBuf[kSourceFaceCapacityOffset], memory_order_relaxed
            );
            const uint base = atomic_fetch_add_explicit(
                &sunDepthBuf[kSourceFaceHeaderOffset], total, memory_order_relaxed
            );
            admissionScratch[uint(faceFrame.admission.y)] = base;
            admissionScratch[uint(faceFrame.admission.y) + 1u] = total;
            if (total > 0u && (base >= capacity || total > capacity - base)) {
                atomic_store_explicit(
                    &sunDepthBuf[sourceFaceHierarchyNeededIndex(capacity)], 1u,
                    memory_order_relaxed
                );
            }
        }
        return;
    }

    const uint index = (groupId.y * uint(faceFrame.dispatch.y) + groupId.x) * 64u + localId.x;
    bool activeVoxel = index < uint(faceFrame.dispatch.x);
    if (activeVoxel) activeVoxel = (voxels[index].colorPacked >> 24u) != 0u;
    if (activeVoxel) activeVoxel = (activeMask[index >> 5u] & (1u << (index & 31u))) != 0u;
    const uint flags = activeVoxel ? (voxels[index].materialFlagBone >> 8u) & 0xFFu : 0u;
    const float subdivisions = float(faceFrame.dispatch.z);
    // Basis 2 preserves authored centers and rotates original faces; basis 1
    // carries the camera-aligned cells produced by detached revoxelization.
    const bool rigidSource = faceFrame.dispatch.w == 2;
    float3 position = float3(0.0);
    float3 worldCenter = float3(0.0);
    bool fogBody = false;
    if (activeVoxel) {
        position = rigidSource ? positions[index].xyz
            : faceFrame.worldOrigin.w != 0.0 ? perAxisRenderedVoxelCenter(positions[index].xyz)
            : float3(roundHalfUp(snapNearIntegerVoxelPosition(positions[index].xyz) * subdivisions)) / subdivisions;
        worldCenter = rotateByQuat(position, faceFrame.viewToWorld) + faceFrame.worldOrigin.xyz;
        fogBody = (voxels[index].reserved & (1u << 3u)) != 0u;
        if (!(fogBody) && sunFrame.fogCeilingEnabled.x != 0 && fogObservers.visionCircleCount > 0 &&
            !fogFieldCastsSunShadow(worldCenter, false, sunFrame.fogCeilingEnabled.x,
                fogShadowCell(worldCenter, fogObservers, fogShadowWindow), fogObservers)) activeVoxel = false;
    }

    float3 projectedCorners[3];
    float3 projectedEdgesU[3];
    float3 projectedEdgesV[3];
    uint faceMarkers[3];
    uint faceMask = 0u;
    uint recordMask = 0u;
    for (int axis = 0; activeVoxel && axis < 3; ++axis) {
        float3 normal = float3(0.0);
        normal[axis] = 1.0;
        const float3 worldAxis = rotateByQuat(normal, faceFrame.viewToWorld);
        const bool positive = dot(worldAxis, sunFrame.sunDirection.xyz) > 0.0;
        const int faceId = axis * 2 + (positive ? 1 : 0);
        if (!faceIsExposed(flags, faceId)) {
            if (fogBody || sunFrame.fogCeilingEnabled.x == 0 || fogObservers.visionCircleCount == 0) continue;
            const float3 neighbor = worldCenter + (positive ? worldAxis : -worldAxis);
            if (!fogVoxelFaceCastsSunShadow(false, worldCenter, positive ? worldAxis : -worldAxis,
                    false, sunFrame.fogCeilingEnabled.x,
                    fogShadowCell(neighbor, fogObservers, fogShadowWindow), fogObservers)) continue;
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
        // Arbitrary source rotations cannot use the shared world/camera face basis.
        const uint faceMarker = sunVoxelFaceMarker(faceId, faceFrame.dispatch.w);
        const float3 projected = sunSpaceProject(corner, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz);
        const float3 projectedU = sunSpaceProject(edgeU, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz);
        const float3 projectedV = sunSpaceProject(edgeV, sunFrame.sunBasisU.xyz, sunFrame.sunBasisV.xyz, sunFrame.sunDirection.xyz);
        projectedCorners[axis] = projected;
        projectedEdgesU[axis] = projectedU;
        projectedEdgesV[axis] = projectedV;
        faceMarkers[axis] = faceMarker;
        faceMask |= 1u << uint(axis);
        if (sourceSunFaceNeedsRecord(projected, projectedU, projectedV, sunFrame)) {
            recordMask |= 1u << uint(axis);
        }
    }

    sharedRecordCounts[lane] = popcount(recordMask);
    threadgroup_barrier(mem_flags::mem_threadgroup);
    const uint group = groupId.y * uint(faceFrame.dispatch.y) + groupId.x;
    if (faceFrame.admission.x == 0) {
        if (lane == 0u) {
            uint total = 0u;
            for (uint item = 0u; item < 64u; ++item) total += sharedRecordCounts[item];
            admissionScratch[group] = total;
        }
        return;
    }

    if (lane == 0u) {
        uint prefix = 0u;
        for (uint item = 0u; item < 64u; ++item) {
            const uint count = sharedRecordCounts[item];
            sharedRecordCounts[item] = prefix;
            prefix += count;
        }
    }
    threadgroup_barrier(mem_flags::mem_threadgroup);
    uint faceIndex = admissionScratch[uint(faceFrame.admission.y)] +
        admissionScratch[group] + sharedRecordCounts[lane];
    for (int axis = 0; axis < 3; ++axis) {
        const uint bit = 1u << uint(axis);
        if ((faceMask & bit) == 0u) continue;
        if ((recordMask & bit) != 0u) {
            indexSourceSunFaceAssigned(
                sunDepthBuf, projectedCorners[axis], projectedEdgesU[axis],
                projectedEdgesV[axis], sunFrame, faceIndex
            );
            ++faceIndex;
        }
        rasterSunFace(sunDepthBuf, projectedCorners[axis], projectedEdgesU[axis],
            projectedEdgesV[axis], sunFrame.cascadeOriginUV_0, sunFrame.cascadeTexelSize_0,
            int(kSourceFaceFallbackOffset), faceMarkers[axis]);
        rasterSunFace(sunDepthBuf, projectedCorners[axis], projectedEdgesU[axis],
            projectedEdgesV[axis], sunFrame.cascadeOriginUV_1, sunFrame.cascadeTexelSize_1,
            kCascadeTexelCount + int(kSourceFaceFallbackOffset), faceMarkers[axis]);
    }
}
