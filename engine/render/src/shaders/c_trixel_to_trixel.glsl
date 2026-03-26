#version 460 core

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(std140, binding = 10) uniform FrameDataTrixelToTrixel {
    uniform ivec2 cameraTrixelOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 trixelTextureOffsetZ1;
    uniform vec2 texturePos2DIso;
};

layout(rgba8, binding = 0) uniform image2D trixelColorsTo;
layout(r32i, binding = 1) uniform iimage2D trixelDistancesTo;
layout(rgba8, binding = 2) readonly uniform image2D trixelColorsFrom;
layout(r32i, binding = 3) readonly uniform iimage2D trixelDistancesFrom;

void main() {
    ivec2 srcPixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 srcSize = imageSize(trixelColorsFrom);
    if (srcPixel.x >= srcSize.x || srcPixel.y >= srcSize.y) {
        return;
    }

    vec4 srcColor = imageLoad(trixelColorsFrom, srcPixel);
    if (srcColor.a == 0.0) {
        return;
    }

    int srcDistance = imageLoad(trixelDistancesFrom, srcPixel).x;
    if (srcDistance >= 65535) {
        return;
    }

    ivec2 srcOrigin = trixelTextureOffsetZ1;
    ivec2 dstOrigin = trixelCanvasOffsetZ1 + cameraTrixelOffset;
    ivec2 isoOffset = ivec2(floor(texturePos2DIso));
    ivec2 dstPixel = (srcPixel - srcOrigin) + dstOrigin + isoOffset;

    ivec2 dstSize = imageSize(trixelColorsTo);
    if (dstPixel.x < 0 || dstPixel.x >= dstSize.x ||
        dstPixel.y < 0 || dstPixel.y >= dstSize.y) {
        return;
    }

    int dstDistance = imageLoad(trixelDistancesTo, dstPixel).x;
    if (srcDistance <= dstDistance) {
        imageStore(trixelColorsTo, dstPixel, srcColor);
        imageStore(trixelDistancesTo, dstPixel, ivec4(srcDistance, 0, 0, 0));
    }
}
