/*
 * Project: Irreden Engine
 * File: c_trixel_to_trixel.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: January 2024
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 460 core

layout(local_size_x = 1, local_size_y = 1, local_size_z = 1) in;

layout(std140, binding = 1) uniform GlobalConstants {
    uniform ivec2 kCanvasTriangleOriginOffsetX1;
    uniform ivec2 kCanvasTriangleOriginOffsetZ1;
    uniform int kMinTriangleDistance;
    uniform int kMaxTriangleDistance;
};

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
};

layout(rgba8, binding = 0) uniform image2D trixelColorsTo;
layout(r32i, binding = 1) uniform iimage2D trixelDistancesTo;
layout(rgba8, binding = 2) uniform image2D trixelColorsFrom;
layout(r32i, binding = 3) uniform iimage2D trixelDistancesFrom;

vec4 unpackColor(uint packedColor) {
    return vec4(
        float(packedColor & 0xFFu) / 255.0,
        float((packedColor >> 8) & 0xFFu) / 255.0,
        float((packedColor >> 16) & 0xFFu) / 255.0,
        float((packedColor >> 24) & 0xFFu) / 255.0
    );
}

void main() {
    const vec4 color = unpackColor(colors[gl_WorkGroupID.x]);
    if(color.a == 0) {
        return;
    }
    const vec4 voxelPosition = positions[gl_WorkGroupID.x];
    const ivec3 voxelPositionInt = ivec3(
        round(voxelPosition.x),
        round(voxelPosition.y),
        round(voxelPosition.z)
    );
    const int voxelDistance = pos3DtoDistance(voxelPositionInt);
    const ivec2 canvasPixel =
        trixelCanvasOffsetZ1 +
        ivec2(floor(frameCanvasOffset.x), floor(frameCanvasOffset.y)) +
        ivec2(gl_LocalInvocationID.x, gl_LocalInvocationID.y) +
        pos3DtoPos2DIso(voxelPositionInt);

    if (canvasPixel.x < 0 || canvasPixel.x >= imageSize(trixelDistancesTo).x) {
        return;
    }

    if (canvasPixel.y < 0 || canvasPixel.y >= imageSize(trixelDistancesTo).y) {
        return;
    }

    int canvasDistance = imageAtomicMin(
        trixelDistancesTo,
        canvasPixel,
        voxelDistance
    );
}