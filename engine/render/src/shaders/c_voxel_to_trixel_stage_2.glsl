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
    // Model-frame iso depth axis `R⁻¹·(1,1,1)` for the per-voxel occlusion
    // metric (#1462). Stage 2 MUST read the same axis stage 1 wrote the
    // distance tap on, or the depth re-test below rejects every detached
    // color tap off a snap (the #1499 sliver). (1,1,1) for world / identity
    // so the GRID path stays byte-identical. Appended after visibleFaceIds
    // (offset 144) to match the CPU struct + stage 1's binding-7 layout.
    uniform vec4 voxelDepthAxis;
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
    const uint voxelIndex,
    const int faceId,
    const bool reVoxelize
) {
    int maxN = isDetachedCanvas > 0.5 ? 6 : 1;
    int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    float inv = 1.0 / float(n);
    // Conservative coverage (#1557 Option B): mirror stage 1's re-voxelize
    // footprint dilation so the colour/entity tap reaches the same gap pixels
    // the distance tap claimed. writeColorTap's depth re-test then paints only
    // the occlusion-winning face per pixel, so gaps get the correct colour.
    ivec2 su = ivec2(0);
    ivec2 sv = ivec2(0);
    if (reVoxelize) {
        faceInPlaneIsoSteps(faceId, su, sv);
    }
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            vec2 src = vec2(gl_LocalInvocationID.xy) + vec2(float(sx), float(sy)) * inv;
            ivec2 p = base + roundHalfUp(D * src);
            writeColorTap(p, voxelDistance, voxelColor, voxelIndex);
            if (reVoxelize) {
                writeColorTap(p + su, voxelDistance, voxelColor, voxelIndex);
                writeColorTap(p - su, voxelDistance, voxelColor, voxelIndex);
                writeColorTap(p + sv, voxelDistance, voxelColor, voxelIndex);
                writeColorTap(p - sv, voxelDistance, voxelColor, voxelIndex);
            }
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

    // Re-voxelize marker — mirror of stage 1.
    const bool reVoxelize = visibleFaceIds.w != 0;

    // Stage 2 mirrors stage 1's exposed-face gate so it doesn't waste an
    // `imageLoad` + depth compare on faces stage 1 already skipped — and is
    // BYPASSED for re-voxelize for the same reason (#1570): its `flags_` mask is
    // in the unrotated authoring frame, not the baked-in rotated cell frame (see
    // c_voxel_to_trixel_stage_1.glsl). Stage 1 emitted all three cardinal faces
    // for re-voxelize, so stage 2 must paint them too or the colour tap is lost.
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;
    if (!reVoxelize && !faceIsExposed(flagsByte, faceId)) return;

    // Per-slot deformation matrix — see stage 1 for the contract.
    const mat2 D = mat2(faceDeform[slot].xy, faceDeform[slot].zw);

    // Smooth camera Z-yaw per-axis routing (T2 / #1309 + T3 / #1310) — mirrors
    // stage 1's geometry exactly so the color/entity-id tap lands on the same
    // single center cell the distance tap did. T3 stores one cell per face
    // center (not the emitDeformedFace cluster); the framebuffer scatter
    // reconstructs the face quad. See c_voxel_to_trixel_stage_1.glsl.
    if (perAxisRoute != 0) {
        const int axis = perAxisRoute - 1;
        if ((faceId >> 1) != axis) return;
        // Face-local in-plane store — mirrors stage 1 exactly so the color tap
        // lands on the same cell the distance tap did. See
        // c_voxel_to_trixel_stage_1.glsl / ir_iso_common.glsl (#1310 fix).
        const ivec2 perAxisBase =
            trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);
        const ivec2 canvasSize = imageSize(triangleCanvasDistances);
        const ivec3 anchor = faceLocalAnchor(perAxisBase, canvasSize);
        const ivec2 cellBase = faceLocalBase(axis, anchor, canvasSize);
        if (voxelRenderOptions.x == 0) {
            const ivec3 worldPos = ivec3(round(voxelPosition.xyz));
            // Mirror stage 1's face-plane store (#1310 seam fix) so the color
            // tap lands on the same cell + depth the distance tap did.
            const ivec3 facePos = faceMicroPositionFixed6(faceId, worldPos, 0, 0, 1);
            // No sub-cell offset at base resolution; encode centre fracs (8,8).
            const int voxelDistance =
                encodeDepthWithFaceFrac(pos3DtoDistance(facePos), slot, 8, 8);
            writeColorTap(
                cellBase + faceInPlaneCoords(faceId, facePos), voxelDistance,
                voxelColor, voxelIndex
            );
            return;
        }
        // #1458: mirror stage 1's base-resolution store (z=0 only).
        if (gl_WorkGroupID.z != 0) return;
        const vec3 worldAligned_s2 = snapNearIntegerVoxelPosition(voxelPosition.xyz);
        const ivec3 worldPos_s2 = ivec3(round(worldAligned_s2));
        const ivec3 facePos_s2 = faceMicroPositionFixed6(faceId, worldPos_s2, 0, 0, 1);
        const vec3 fracInCell_s2 = worldAligned_s2 - vec3(worldPos_s2);
        const int voxelDistance_s2 =
            encodeDepthWithFaceFrac(pos3DtoDistance(facePos_s2), slot, axis, fracInCell_s2);
        writeColorTap(
            cellBase + faceInPlaneCoords(faceId, facePos_s2), voxelDistance_s2,
            voxelColor, voxelIndex
        );
        return;
    }

    if (voxelRenderOptions.x == 0) {
        ivec3 voxelPositionInt = ivec3(round(voxelPosition.xyz));
        if (cardinalIndex != 0) {
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
            voxelPositionInt += cardinalLowerCornerShift(cardinalIndex);
        }
        // Detached entities project occlusion depth onto the entity-rotated
        // iso axis (#1462); world/GRID keeps the fixed (1,1,1) via
        // pos3DtoDistance. MUST mirror stage 1's distance-tap depth or the
        // re-test in writeColorTap rejects the tap (#1499).
        const int rawDepth = isDetachedCanvas > 0.5
            ? isoDepthAlongAxis(voxelPositionInt, voxelDepthAxis.xyz)
            : pos3DtoDistance(voxelPositionInt);
        const int voxelDistance = encodeDepthWithFace(rawDepth, slot);
        const ivec2 base =
            trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions) +
            pos3DtoPos2DIso(voxelPositionInt);
        emitDeformedFace(base, D, voxelDistance, voxelColor, voxelIndex, faceId, reVoxelize);
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
    // Detached: mirror stage 1's entity-rotated occlusion axis (#1462 / #1499);
    // depth is in subdivision units on both branches so the encode is unchanged.
    const int depthBase = isDetachedCanvas > 0.5
        ? isoDepthAlongAxis(microPositionFixed, voxelDepthAxis.xyz)
        : (microPositionFixed.x + microPositionFixed.y + microPositionFixed.z);
    const int voxelDistance = encodeDepthWithFace(depthBase, slot);
    const ivec2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(base, D, voxelDistance, voxelColor, voxelIndex, faceId, reVoxelize);
}
