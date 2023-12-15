/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_2.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 460 core

layout(local_size_x = 2, local_size_y = 3, local_size_z = 1) in;

layout(std140, binding = 1) uniform GlobalConstants {
    uniform ivec2 kCanvasTriangleOriginOffsetX1;
    uniform ivec2 kCanvasTriangleOriginOffsetZ1;
    uniform int kMinTriangleDistance;
    uniform int kMaxTriangleDistance;
};

layout(std140, binding = 7) uniform FrameDataCommon {
    uniform vec2 frameCanvasOffset;
};

layout(std430, binding = 5) buffer PositionBuffer {
    vec4 positions[];
};

layout(std430, binding = 6) buffer ColorBuffer {
    uint colors[];
};

layout(rgba8, binding = 0) writeonly uniform image2D triangleCanvasColors;
layout(r32i, binding = 1) uniform iimage2D triangleCanvasDistances;

ivec2 pos3DtoPos2DIso(const ivec3 position) {
    return ivec2(
        - position.x + position.y,
        - position.x - position.y + (2 * position.z)
    );
}

int pos3DtoDistance(ivec3 position) {
    return position.x + position.y + position.z;
}

vec4 unpackColor(uint packedColor) {
    return vec4(
        float(packedColor & 0xFFu) / 255.0,
        float((packedColor >> 8) & 0xFFu) / 255.0,
        float((packedColor >> 16) & 0xFFu) / 255.0,
        float((packedColor >> 24) & 0xFFu) / 255.0
    );
}

const int kXFace = 0;
const int kYFace = 1;
const int kZFace = 2;

int localIDToFace() {
    if(gl_LocalInvocationID.y == 0) {
        return kZFace;
    }
    if(gl_LocalInvocationID.x == 1) {
        return kXFace;
    }
    if(gl_LocalInvocationID.x == 0) {
        return kYFace;
    }
}

// Temp way to fake a light source.
vec4 adjustColorForFace(vec4 color, int face) {
    if(face == kXFace) {
        return color;
    }
    if(face == kYFace) {
        return color - vec4(0.2, 0.2, 0.2, 0.0);
    }
    if(face == kZFace) {
        return color + vec4(0.2, 0.2, 0.2, 0.0);
    }
    return color;
}

void main() {
    const vec4 voxelPosition = positions[gl_WorkGroupID.x];
    const ivec3 voxelPositionInt = ivec3(
        round(voxelPosition.x),
        round(voxelPosition.y),
        round(voxelPosition.z)
    );
    const int voxelDistance = pos3DtoDistance(voxelPositionInt);
    const ivec2 canvasPixel =
        kCanvasTriangleOriginOffsetZ1 +
        ivec2(floor(frameCanvasOffset.x), floor(frameCanvasOffset.y)) +
        ivec2(gl_LocalInvocationID.x, gl_LocalInvocationID.y) +
        pos3DtoPos2DIso(voxelPositionInt);

    if (canvasPixel.x < 0 || canvasPixel.x >= imageSize(triangleCanvasDistances).x) {
        return;
    }
    if (canvasPixel.y < 0 || canvasPixel.y >= imageSize(triangleCanvasDistances).y) {
        return;
    }

    int canvasDistance = imageLoad(triangleCanvasDistances, canvasPixel).x;

    if (voxelDistance == canvasDistance)
    {
        vec4 voxelColor = unpackColor(
            colors[gl_WorkGroupID.x]
        );
        if(voxelColor.a == 0) return;
        voxelColor = adjustColorForFace(
            voxelColor,
            localIDToFace()
        );
        imageStore(
            triangleCanvasColors,
            canvasPixel,
            voxelColor
        );
    }
}

