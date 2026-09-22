#version 450 core

// Rasterizes the exposed sun-facing faces of the cells a canvas rasterizes
// (bindings 5/6/8: the same positions, colors and active mask stage 1 reads)
// into the sun depth map. A revoxelized canvas therefore casts its resampled
// lattice, the geometry its receiver reads and its display shows.
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#include "ir_iso_common.glsl"
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
layout(std140, binding = 16) uniform VoxelSunFaceFrame {
    vec4 worldOrigin;
    vec4 viewToWorld;
    ivec4 dispatch;
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

void indexSourceSunFace(vec3 corner, vec3 edgeU, vec3 edgeV) {
    const float determinant = projectedFaceDeterminant(edgeU.xy, edgeV.xy);
    if (abs(determinant) < 0.000001) return;
    uint faceIndex = 0xFFFFFFFFu;
    const vec2 low = min(min(corner.xy, corner.xy + edgeU.xy), min(corner.xy + edgeV.xy, corner.xy + edgeU.xy + edgeV.xy));
    const vec2 high = max(max(corner.xy, corner.xy + edgeU.xy), max(corner.xy + edgeV.xy, corner.xy + edgeU.xy + edgeV.xy));
    for (uint cascade = 0u; cascade < kSourceFaceCascadeCount; ++cascade) {
        const vec2 origin = cascade == 0u ? cascadeOriginUV_0 : cascadeOriginUV_1;
        const vec2 cellSize = (cascade == 0u ? cascadeTexelSize_0 : cascadeTexelSize_1) * float(kSourceFaceTileEdge);
        const ivec2 first = max(ivec2(floor((low - origin) / cellSize)), ivec2(0));
        const ivec2 last = min(ivec2(floor((high - origin) / cellSize)), ivec2(kSourceFaceTilesPerAxis - 1u));
        if (first.x > last.x || first.y > last.y) continue;
        if (faceIndex == 0xFFFFFFFFu) {
            faceIndex = atomicAdd(sunDepthBuf[kSourceFaceHeaderOffset], 1u);
            if (faceIndex < kSourceFaceCapacity) {
                const uint record = kSourceFaceRecordOffset + faceIndex * kSourceFaceRecordWords;
                for (uint axis = 0; axis < 3u; ++axis) {
                    sunDepthBuf[record + axis] = floatBitsToUint(corner[axis]);
                    sunDepthBuf[record + 3u + axis] = floatBitsToUint(edgeU[axis]);
                    sunDepthBuf[record + 6u + axis] = floatBitsToUint(edgeV[axis]);
                }
            }
        }
        for (int y = first.y; y <= last.y; ++y) for (int x = first.x; x <= last.x; ++x) {
            const uint tile = cascade * kSourceFaceTilesPerAxis * kSourceFaceTilesPerAxis + uint(y) * kSourceFaceTilesPerAxis + uint(x);
            const uint base = sourceFaceTileBase(tile);
            if (faceIndex >= kSourceFaceCapacity) {
                atomicMax(sunDepthBuf[base], kSourceFaceTileCapacity + 1u);
                continue;
            }
            const uint slot = atomicAdd(sunDepthBuf[base], 1u);
            if (slot < kSourceFaceTileCapacity) sunDepthBuf[base + 1u + slot] = faceIndex;
        }
    }
}

void main() {
    const uint index = (gl_WorkGroupID.y * uint(dispatch.y) + gl_WorkGroupID.x) * 64u + gl_LocalInvocationID.x;

    if (index >= uint(dispatch.x)) return;
    if ((voxels[index].colorPacked >> 24u) == 0u) return;
    if ((activeMask[index >> 5u] & (1u << (index & 31u))) == 0u) return;
    const uint flags = (voxels[index].materialFlagBone >> 8u) & 0xFFu;
    const float subdivisions = float(dispatch.z);
    // Basis 2 preserves authored centers and rotates original faces; basis 1
    // carries the camera-aligned cells produced by detached revoxelization.
    const bool rigidSource = dispatch.w == 2;
    const vec3 position = rigidSource ? positions[index].xyz
        : vec3(roundHalfUp(snapNearIntegerVoxelPosition(positions[index].xyz) * subdivisions)) / subdivisions;
    for (int axis = 0; axis < 3; ++axis) {
        vec3 normal = vec3(0.0);
        normal[axis] = 1.0;
        const vec3 worldAxis = rotateByQuat(normal, viewToWorld);
        const bool positive = dot(worldAxis, sunDirection.xyz) > 0.0;
        const int faceId = axis * 2 + (positive ? 1 : 0);
        if (!faceIsExposed(flags, faceId)) continue;
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
        indexSourceSunFace(projected, projectedU, projectedV);
        rasterSunFace(projected, projectedU, projectedV, cascadeOriginUV_0, cascadeTexelSize_0, int(kSourceFaceFallbackOffset), faceMarker);
        rasterSunFace(projected, projectedU, projectedV, cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount + int(kSourceFaceFallbackOffset), faceMarker);
    }
}
