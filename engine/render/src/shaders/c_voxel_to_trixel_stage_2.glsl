#version 460 core

layout(local_size_x = 2, local_size_y = 3, local_size_z = 1) in;

#include "ir_iso_common.glsl"

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    uniform int _voxelDispatchPadding;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
};

layout(std430, binding = 5) readonly buffer PositionBuffer {
    vec4 positions[];
};

layout(std430, binding = 6) readonly buffer ColorBuffer {
    uint colors[];
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

void main() {
    uint compactedIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * numGroupsX;
    if (compactedIdx >= visibleCount) return;

    uint voxelIndex = compactedVoxelIndices[compactedIdx];
    const vec4 voxelPosition = positions[voxelIndex];
    vec4 voxelColor = unpackColor(colors[voxelIndex]);
    int face = localIDToFace_2x3();
    voxelColor = adjustColorForFace(voxelColor, face);

    if (voxelRenderOptions.x == 0) {
        const ivec3 voxelPositionInt = ivec3(round(voxelPosition.xyz));
        const int voxelDistance = encodeDepthWithFace(
            pos3DtoDistance(voxelPositionInt), face);
        const ivec2 canvasPixel =
            trixelCanvasOffsetZ1 +
            ivec2(floor(frameCanvasOffset)) +
            ivec2(gl_LocalInvocationID.xy) +
            pos3DtoPos2DIso(voxelPositionInt);
        writeColorTap(canvasPixel, voxelDistance, voxelColor, voxelIndex);
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
    writeColorTap(canvasPixel, voxelDistance, voxelColor, voxelIndex);
}
