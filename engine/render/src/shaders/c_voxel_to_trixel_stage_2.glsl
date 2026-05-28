#version 450 core

layout(local_size_x = 2, local_size_y = 3, local_size_z = 1) in;

#include "ir_iso_common.glsl"

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (see stage 1 + #1309).
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    // Pre-T-293 screen-space residual composite (T-058 / T-322) retired by
    // T-323; consumed inline via faceDeform[] in the trixel emit below.
    uniform float residualYaw;
    // 1.0 for a detached entity canvas, 0.0 for the world canvas — see
    // c_voxel_to_trixel_stage_1.glsl for the super-sampling contract.
    uniform float isDetachedCanvas;
    // Per-slot deformation matrix packed column-major into vec4 (see
    // c_voxel_to_trixel_stage_1.glsl for the layout). T-293 + #1278.
    uniform vec4 faceDeform[3];
    // Per-slot world FaceId (0..5). See stage 1 + #1278 for the contract.
    uniform ivec4 visibleFaceIds;
};

layout(std430, binding = 5) readonly buffer PositionBuffer {
    vec4 positions[];
};

// 12 B per voxel — must match C_Voxel layout in
// engine/prefabs/irreden/voxel/components/component_voxel.hpp.
struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};

layout(std430, binding = 6) readonly buffer ColorBuffer {
    Voxel voxels[];
};

layout(std430, binding = 13) readonly buffer EntityIdBuffer {
    uvec2 entityIds[];
};

layout(std430, binding = 25) readonly buffer CompactedIndices {
    uint compactedVoxelIndices[];
};

layout(std430, binding = 26) readonly buffer IndirectDispatchParams {
    uint numGroupsX;
    uint numGroupsY;
    uint numGroupsZ;
    uint visibleCount;
};

layout(rgba8, binding = 0) writeonly uniform image2D triangleCanvasColors;
layout(r32i, binding = 1) uniform iimage2D triangleCanvasDistances;
layout(rg32ui, binding = 2) writeonly uniform uimage2D triangleCanvasEntityIds;

void writeColorTap(
    const ivec2 canvasPixel,
    const int voxelDistance,
    const vec4 voxelColor,
    const uint voxelIndex
) {
    if (!isInsideCanvas(canvasPixel, imageSize(triangleCanvasDistances))) return;
    int canvasDistance = imageLoad(triangleCanvasDistances, canvasPixel).x;
    if (voxelDistance == canvasDistance) {
        imageStore(triangleCanvasColors, canvasPixel, voxelColor);
        imageStore(triangleCanvasEntityIds, canvasPixel,
                   uvec4(entityIds[voxelIndex], 0u, 0u));
    }
}

// Emit a face's 2x3 trixel block through the deformation matrix D.
// Super-sampling gated by isDetachedCanvas — see stage-1 for the contract.
void emitDeformedFace(
    const ivec2 base,
    const mat2 D,
    const int voxelDistance,
    const vec4 voxelColor,
    const uint voxelIndex
) {
    int maxN = isDetachedCanvas > 0.5 ? 6 : 1;
    int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    float inv = 1.0 / float(n);
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            vec2 src = vec2(gl_LocalInvocationID.xy) + vec2(float(sx), float(sy)) * inv;
            writeColorTap(base + roundHalfUp(D * src), voxelDistance, voxelColor, voxelIndex);
        }
    }
}

void main() {
    uint compactedIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * numGroupsX;
    if (compactedIdx >= visibleCount) return;

    uint voxelIndex = compactedVoxelIndices[compactedIdx];
    const vec4 voxelPosition = positions[voxelIndex];
    vec4 voxelColor = unpackColor(voxels[voxelIndex].colorPacked);
    // See c_voxel_to_trixel_stage_1.glsl for the slot/faceId contract (#1278).
    const int slot = localIDToFace_2x3(gl_LocalInvocationID.xy);
    const int faceId = visibleFaceIds[slot];

    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);

    // Stage 2 mirrors stage 1's exposed-face gate so it doesn't waste an
    // `imageLoad` + depth compare on faces stage 1 already skipped.
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;
    if (!faceIsExposed(flagsByte, faceId)) return;

    // Per-slot deformation matrix — see stage 1 for the contract.
    const mat2 D = mat2(faceDeform[slot].xy, faceDeform[slot].zw);

    // Smooth camera Z-yaw per-axis routing (T2 / #1309) — mirrors stage 1's
    // geometry exactly so the color/entity-id taps land on the same trixels the
    // distance taps did. See c_voxel_to_trixel_stage_1.glsl for the contract.
    if (perAxisRoute != 0) {
        if ((faceId >> 1) != perAxisRoute - 1) return;
        const ivec2 perAxisBase =
            trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
        if (voxelRenderOptions.x == 0) {
            const vec3 worldPos = round(voxelPosition.xyz);
            const int voxelDistance =
                encodeDepthWithFace(pos3DtoDistance(ivec3(worldPos)), slot);
            const ivec2 base =
                perAxisBase + roundHalfUp(pos3DtoPos2DIsoYawed(worldPos, visualYaw));
            emitDeformedFace(base, D, voxelDistance, voxelColor, voxelIndex);
            return;
        }
        const int subPerAxis = max(voxelRenderOptions.y, 1);
        const int uPerAxis = int(gl_WorkGroupID.z) / subPerAxis;
        const int vPerAxis = int(gl_WorkGroupID.z) % subPerAxis;
        const vec3 worldAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
        const ivec3 worldFixed = ivec3(round(worldAligned * float(subPerAxis)));
        const ivec3 microWorld =
            faceMicroPositionFixed6(faceId, worldFixed, uPerAxis, vPerAxis, subPerAxis);
        const int voxelDistance =
            encodeDepthWithFace(microWorld.x + microWorld.y + microWorld.z, slot);
        const ivec2 base =
            perAxisBase + roundHalfUp(pos3DtoPos2DIsoYawed(vec3(microWorld), visualYaw));
        emitDeformedFace(base, D, voxelDistance, voxelColor, voxelIndex);
        return;
    }

    if (voxelRenderOptions.x == 0) {
        ivec3 voxelPositionInt = ivec3(round(voxelPosition.xyz));
        if (cardinalIndex != 0) {
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
            voxelPositionInt += cardinalLowerCornerShift(cardinalIndex);
        }
        const int voxelDistance = encodeDepthWithFace(
            pos3DtoDistance(voxelPositionInt), slot);
        const ivec2 base =
            trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions) +
            pos3DtoPos2DIso(voxelPositionInt);
        emitDeformedFace(base, D, voxelDistance, voxelColor, voxelIndex);
        return;
    }

    const int subdivisions = max(voxelRenderOptions.y, 1);
    int u = int(gl_WorkGroupID.z) / subdivisions;
    int v = int(gl_WorkGroupID.z) % subdivisions;

    const vec3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const ivec3 voxelPositionFixed = ivec3(round(voxelPositionAligned * float(subdivisions)));
    const ivec2 frameOffsetFixed =
        trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);

    ivec3 microPositionFixed =
        faceMicroPositionFixed6(faceId, voxelPositionFixed, u, v, subdivisions);
    if (cardinalIndex != 0) {
        microPositionFixed = rotateCardinalZ(microPositionFixed, cardinalIndex);
        // Shift is per-world-unit; scale to subdivision units to match
        // `voxelPositionFixed = round(worldPos * subdivisions)`.
        microPositionFixed += cardinalLowerCornerShift(cardinalIndex) * subdivisions;
    }
    const int depthBase =
        microPositionFixed.x + microPositionFixed.y + microPositionFixed.z;
    const int voxelDistance = encodeDepthWithFace(depthBase, slot);
    const ivec2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(base, D, voxelDistance, voxelColor, voxelIndex);
}
