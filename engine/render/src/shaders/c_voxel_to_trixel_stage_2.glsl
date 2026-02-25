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

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
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
    return kZFace;
}

ivec3 faceMicroPositionFixed(int face, ivec3 voxelPositionFixed, int u, int v, int subdivisions) {
    if (face == kXFace) {
        return ivec3(
            voxelPositionFixed.x,
            voxelPositionFixed.y + u,
            voxelPositionFixed.z + v
        );
    }
    if (face == kYFace) {
        return ivec3(
            voxelPositionFixed.x + u,
            voxelPositionFixed.y,
            voxelPositionFixed.z + v
        );
    }
    return ivec3(
        voxelPositionFixed.x + u,
        voxelPositionFixed.y + v,
        voxelPositionFixed.z
    );
}

vec3 snapNearIntegerVoxelPosition(vec3 voxelPosition) {
    const vec3 voxelRounded = round(voxelPosition);
    const bvec3 nearGrid = lessThanEqual(abs(voxelPosition - voxelRounded), vec3(0.0001));
    return mix(voxelPosition, voxelRounded, vec3(nearGrid));
}

vec4 adjustColorForFace(vec4 color, int face) {
    float b = 1.0;
    if (face == kYFace) b = 0.75;
    if (face == kZFace) b = 1.25;
    return vec4(clamp(color.rgb * b, 0.0, 1.0), color.a);
}

void writeColorTap(const ivec2 canvasPixel, const int voxelDistance, const vec4 voxelColor) {
    if (canvasPixel.x < 0 || canvasPixel.x >= imageSize(triangleCanvasDistances).x) {
        return;
    }
    if (canvasPixel.y < 0 || canvasPixel.y >= imageSize(triangleCanvasDistances).y) {
        return;
    }
    int canvasDistance = imageLoad(triangleCanvasDistances, canvasPixel).x;
    if (voxelDistance == canvasDistance) {
        imageStore(triangleCanvasColors, canvasPixel, voxelColor);
    }
}

void main() {
    const vec4 voxelPosition = positions[gl_WorkGroupID.x];
    vec4 voxelColor = unpackColor(colors[gl_WorkGroupID.x]);
    if(voxelColor.a == 0) return;
    voxelColor = adjustColorForFace(voxelColor, localIDToFace());

    if (voxelRenderOptions.x == 0) {
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
        writeColorTap(canvasPixel, voxelDistance, voxelColor);
        return;
    }

    const int subdivisions = max(voxelRenderOptions.y, 1);
    const vec3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const ivec3 voxelPositionFixed = ivec3(round(voxelPositionAligned * float(subdivisions)));
    const ivec2 frameOffsetFixed =
        trixelCanvasOffsetZ1 +
        ivec2(floor(frameCanvasOffset * float(subdivisions)));
    const ivec2 localFaceOffsetFixed =
        ivec2(gl_LocalInvocationID.x, gl_LocalInvocationID.y);
    const int face = localIDToFace();

    for (int u = 0; u < subdivisions; ++u) {
        for (int v = 0; v < subdivisions; ++v) {
                const ivec3 microPositionFixed =
                    faceMicroPositionFixed(face, voxelPositionFixed, u, v, subdivisions);
                const int depthBase =
                    microPositionFixed.x + microPositionFixed.y + microPositionFixed.z;
                const int voxelDistance = depthBase * 4 + face;
                const ivec2 canvasPixel =
                    frameOffsetFixed + localFaceOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
                writeColorTap(canvasPixel, voxelDistance, voxelColor);
        }
    }
}

