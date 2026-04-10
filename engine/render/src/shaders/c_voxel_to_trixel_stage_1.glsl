/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_1.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 460 core

layout(local_size_x = 2, local_size_y = 3, local_size_z = 1) in;

#include "ir_iso_common.glsl"

// Coordinate chain: World 3D -> Iso 2D -> Canvas pixel
//   canvasPixel = trixelCanvasOffsetZ1 + floor(cameraIso) + pos3DtoPos2DIso(world)
// frameCanvasOffset holds floor(cameraIso) from the CPU side.
// Canvas Y increases upward; the trixel-to-framebuffer pass flips V.
layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;         // floor(cameraIso)
    uniform ivec2 trixelCanvasOffsetZ1;     // canvas origin for Z-face voxels
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    uniform int _voxelDispatchPadding;
    uniform ivec2 canvasSizePixels;         // trixel canvas dimensions
    uniform ivec2 cullIsoMin;               // iso-space cull viewport (matches CPU chunk mask)
    uniform ivec2 cullIsoMax;
};

layout(std430, binding = 5) readonly buffer PositionBuffer {
    vec4 positions[];
};

layout(std430, binding = 6) readonly buffer ColorBuffer {
    uint colors[];
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

layout(r32i, binding = 1) uniform iimage2D triangleCanvasDistances;

void writeDistanceTap(const ivec2 canvasPixel, const int voxelDistance) {
    if (!isInsideCanvas(canvasPixel, imageSize(triangleCanvasDistances))) return;
    imageAtomicMin(triangleCanvasDistances, canvasPixel, voxelDistance);
}

void main() {
    uint compactedIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * numGroupsX;
    if (compactedIdx >= visibleCount) return;

    uint voxelIndex = compactedVoxelIndices[compactedIdx];
    const vec4 voxelPosition = positions[voxelIndex];

    const int face = localIDToFace_2x3();

    if (voxelRenderOptions.x == 0) {
        const ivec3 voxelPositionInt = ivec3(round(voxelPosition.xyz));
        const int voxelDistance = encodeDepthWithFace(
            pos3DtoDistance(voxelPositionInt), face);
        const ivec2 canvasPixel =
            trixelCanvasOffsetZ1 +
            ivec2(floor(frameCanvasOffset)) +
            ivec2(gl_LocalInvocationID.xy) +
            pos3DtoPos2DIso(voxelPositionInt);
        writeDistanceTap(canvasPixel, voxelDistance);
        return;
    }

    const int subdivisions = max(voxelRenderOptions.y, 1);
    int u = int(gl_WorkGroupID.z) / subdivisions;
    int v = int(gl_WorkGroupID.z) % subdivisions;

    const vec3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const ivec3 voxelPositionFixed = ivec3(round(voxelPositionAligned * float(subdivisions)));
    const ivec2 frameOffsetFixed =
        trixelCanvasOffsetZ1 +
        ivec2(floor(frameCanvasOffset * float(subdivisions)));

    const ivec3 microPositionFixed =
        faceMicroPositionFixed(face, voxelPositionFixed, u, v, subdivisions);
    const int depthBase =
        microPositionFixed.x + microPositionFixed.y + microPositionFixed.z;
    const int voxelDistance = encodeDepthWithFace(depthBase, face);
    const ivec2 canvasPixel =
        frameOffsetFixed + ivec2(gl_LocalInvocationID.xy) + pos3DtoPos2DIso(microPositionFixed);
    writeDistanceTap(canvasPixel, voxelDistance);
}
