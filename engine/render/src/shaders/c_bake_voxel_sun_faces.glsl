#version 450 core

// Rasterizes the exposed sun-facing faces of the cells a canvas rasterizes
// (bindings 5/6/8: the same positions, colors and active mask stage 1 reads)
// into the sun depth map. A revoxelized canvas therefore casts its resampled
// lattice, the geometry its receiver reads and its display shows.
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#include "ir_iso_common.glsl"
layout(rg32ui, binding = 4) readonly uniform uimage2D fogShadowWindow;
#include "ir_fog_shadow_caster.glsl"
#include "ir_sun_projection.glsl"
#include "ir_sun_face_query_layout.glsl"

struct VoxelSunSample {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};
layout(std430, binding = 8) readonly buffer VoxelActiveMask { uint activeMask[]; };
layout(std430, binding = 5) readonly buffer PositionBuffer { vec4 positions[]; };
layout(std430, binding = 6) readonly buffer ColorBuffer { VoxelSunSample voxels[]; };
layout(std430, binding = 28) restrict buffer SunShadowDepthMap { uint sunDepthBuf[]; };
layout(std430, binding = 26) restrict buffer SunFaceAdmissionScratch { uint admissionScratch[]; };
layout(std140, binding = 16) uniform VoxelSunFaceFrame {
    vec4 worldOrigin; // xyz: world translation; w: per-axis raster enabled
    vec4 viewToWorld;
    ivec4 dispatch;
    ivec4 admission;
};
layout(std140, binding = 29) uniform FrameDataSun {
    uniform vec4 sunDirection;
    uniform float sunIntensity;
    uniform float sunAmbient;
    uniform int shadowsEnabled;
    uniform int aoEnabled;
    uniform vec4 sunBasisU;
    uniform vec4 sunBasisV;
    uniform vec2 sunBufferOriginUV;
    uniform vec2 sunBufferTexelSize;
    uniform vec2 cascadeOriginUV_0;
    uniform vec2 cascadeTexelSize_0;
    uniform vec2 cascadeOriginUV_1;
    uniform vec2 cascadeTexelSize_1;
    uniform float cascadeSplitDepth;
    uniform int cascadeCount;
    uniform float sunSplatMaxTexels;
    uniform float sunMaxShadowThrow;  // Unused here (receiver-only)
    uniform vec4 sunCasterViewToWorld;  // Unused here; places fogCeilingEnabled at 144
    uniform ivec4 fogCeilingEnabled;
};

void rasterSunFace(vec3 corner, vec3 edgeU, vec3 edgeV, vec2 origin, vec2 texelSize, int cascadeOffset, uint faceMarker) {
    const vec2 a = edgeU.xy;
    const vec2 b = edgeV.xy;
    const float determinant = projectedFaceDeterminant(a, b);
    if (abs(determinant) < 0.000001) return;
    const vec2 uvMin = min(min(corner.xy, corner.xy + a), min(corner.xy + b, corner.xy + a + b));
    const vec2 uvMax = max(max(corner.xy, corner.xy + a), max(corner.xy + b, corner.xy + a + b));
    const ivec2 first = max(ivec2(ceil((uvMin - origin) / texelSize - 0.5)), ivec2(0));
    const ivec2 last = min(ivec2(floor((uvMax - origin) / texelSize - 0.5)), ivec2(kSunShadowMapDim - 1));
    if (any(greaterThan(first, last))) return;
    for (int y = first.y; y <= last.y; ++y) {
        for (int x = first.x; x <= last.x; ++x) {
            const vec2 delta = origin + (vec2(x, y) + 0.5) * texelSize - corner.xy;
            const vec2 faceUV = projectedFaceCoordinates(delta, a, b, determinant);
            if (any(lessThan(faceUV, vec2(-0.00001))) || any(greaterThan(faceUV, vec2(1.00001)))) continue;
            const float depth = corner.z + faceUV.x * edgeU.z + faceUV.y * edgeV.z;
            atomicMin(sunDepthBuf[cascadeOffset + y * kSunShadowMapDim + x], (packSunDepth(depth, ivec2(0)) | faceMarker));
        }
    }
}

#include "ir_sun_face_index.glsl"

shared uint sharedRecordCounts[64];

void main() {
    const uint lane = gl_LocalInvocationID.x;
    if (admission.x == 1) {
        if (lane == 0u) {
            uint total = 0u;
            for (uint group = 0u; group < uint(admission.y); ++group) {
                const uint count = admissionScratch[group];
                admissionScratch[group] = total;
                total += count;
            }
            const uint boundWords = uint(sunDepthBuf.length());
            const uint capacity = sourceFaceCapacity(boundWords);
            const uint base = atomicAdd(sunDepthBuf[sourceFaceHeaderIndex(boundWords)], total);
            admissionScratch[uint(admission.y)] = base;
            admissionScratch[uint(admission.y) + 1u] = total;
            if (total > 0u && (base >= capacity || total > capacity - base)) {
                atomicExchange(sunDepthBuf[sourceFaceHierarchyNeededIndex(capacity)], 1u);
            }
        }
        return;
    }

    const uint index = (gl_WorkGroupID.y * uint(dispatch.y) + gl_WorkGroupID.x) * 64u + gl_LocalInvocationID.x;
    bool activeVoxel = index < uint(dispatch.x);
    if (activeVoxel) activeVoxel = (voxels[index].colorPacked >> 24u) != 0u;
    if (activeVoxel) activeVoxel = (activeMask[index >> 5u] & (1u << (index & 31u))) != 0u;
    const uint flags = activeVoxel ? (voxels[index].materialFlagBone >> 8u) & 0xFFu : 0u;
    const float subdivisions = float(dispatch.z);
    // Basis 2 preserves authored centers and rotates original faces; basis 1
    // carries the camera-aligned cells produced by detached revoxelization.
    const bool rigidSource = dispatch.w == 2;
    vec3 position = vec3(0.0);
    vec3 worldCenter = vec3(0.0);
    bool fogBody = false;
    if (activeVoxel) {
        position = rigidSource ? positions[index].xyz
            : worldOrigin.w != 0.0 ? perAxisRenderedVoxelCenter(positions[index].xyz)
            : vec3(roundHalfUp(snapNearIntegerVoxelPosition(positions[index].xyz) * subdivisions)) / subdivisions;
        worldCenter = rotateByQuat(position, viewToWorld) + worldOrigin.xyz;
        fogBody = (voxels[index].reserved & (1u << 3u)) != 0u;
        if (!(fogBody) && fogCeilingEnabled.x != 0 && fogShadowVisionCircleCount > 0 &&
            !fogFieldCastsSunShadow(worldCenter, false, fogCeilingEnabled.x,
                fogShadowCell(worldCenter))) activeVoxel = false;
    }

    vec3 projectedCorners[3];
    vec3 projectedEdgesU[3];
    vec3 projectedEdgesV[3];
    uint faceMarkers[3];
    uint faceMask = 0u;
    uint recordMask = 0u;
    for (int axis = 0; activeVoxel && axis < 3; ++axis) {
        vec3 normal = vec3(0.0);
        normal[axis] = 1.0;
        const vec3 worldAxis = rotateByQuat(normal, viewToWorld);
        const bool positive = dot(worldAxis, sunDirection.xyz) > 0.0;
        const int faceId = axis * 2 + (positive ? 1 : 0);
        if (!faceIsExposed(flags, faceId)) {
            if (fogBody || fogCeilingEnabled.x == 0 || fogShadowVisionCircleCount == 0) continue;
            const vec3 neighbor = worldCenter + (positive ? worldAxis : -worldAxis);
            if (!fogVoxelFaceCastsSunShadow(false, worldCenter, positive ? worldAxis : -worldAxis,
                    false, fogCeilingEnabled.x, fogShadowCell(neighbor))) continue;
        }
        vec3 corner = position - kVoxelRasterCellAnchor;
        corner[axis] += positive ? 1.0 : 0.0;
        vec3 edgeU = vec3(0.0);
        vec3 edgeV = vec3(0.0);
        edgeU[(axis + 1) % 3] = 1.0;
        edgeV[(axis + 2) % 3] = 1.0;
        corner = rotateByQuat(corner, viewToWorld) + worldOrigin.xyz;
        edgeU = rotateByQuat(edgeU, viewToWorld);
        edgeV = rotateByQuat(edgeV, viewToWorld);
        // Arbitrary source rotations cannot use the shared world/camera face basis.
        const uint faceMarker = sunVoxelFaceMarker(faceId, dispatch.w);
        const vec3 projected = sunSpaceProject(corner, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
        const vec3 projectedU = sunSpaceProject(edgeU, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
        const vec3 projectedV = sunSpaceProject(edgeV, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
        projectedCorners[axis] = projected;
        projectedEdgesU[axis] = projectedU;
        projectedEdgesV[axis] = projectedV;
        faceMarkers[axis] = faceMarker;
        faceMask |= 1u << uint(axis);
        if (sourceSunFaceNeedsRecord(projected, projectedU, projectedV)) {
            recordMask |= 1u << uint(axis);
        }
    }

    sharedRecordCounts[lane] = bitCount(recordMask);
    memoryBarrierShared();
    barrier();
    const uint group = gl_WorkGroupID.y * uint(dispatch.y) + gl_WorkGroupID.x;
    if (admission.x == 0) {
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
    memoryBarrierShared();
    barrier();
    uint faceIndex = admissionScratch[uint(admission.y)] + admissionScratch[group] + sharedRecordCounts[lane];
    for (int axis = 0; axis < 3; ++axis) {
        const uint bit = 1u << uint(axis);
        if ((faceMask & bit) == 0u) continue;
        if ((recordMask & bit) != 0u) {
            indexSourceSunFaceAssigned(
                projectedCorners[axis], projectedEdgesU[axis], projectedEdgesV[axis], faceIndex
            );
            ++faceIndex;
        }
        rasterSunFace(projectedCorners[axis], projectedEdgesU[axis], projectedEdgesV[axis],
            cascadeOriginUV_0, cascadeTexelSize_0, int(kSourceFaceFallbackOffset), faceMarkers[axis]);
        rasterSunFace(projectedCorners[axis], projectedEdgesU[axis], projectedEdgesV[axis],
            cascadeOriginUV_1, cascadeTexelSize_1,
            kCascadeTexelCount + int(kSourceFaceFallbackOffset), faceMarkers[axis]);
    }
}
