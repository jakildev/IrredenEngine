#version 450 core

// One image draw onto a trixel canvas: one thread per canvas texel of the
// draw's clipped box, dispatched from `clipLo`.
layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "ir_iso_common.glsl"

struct CanvasImageCommand {
    ivec2 clipLo;
    ivec2 clipHi;
    ivec2 origin;
    ivec2 size;
    vec4 uvRect;           // u0, v0, u1, v1
    ivec2 baseSize;        // level 0 of the atlas
    int levelCount;
    int cell;
    uint tintPacked;
    int posterizeLevels;   // 0 = off
    int distance;
    int pad;
};

layout(std430, binding = 12) buffer CommandBuffer {
    CanvasImageCommand command;
};

layout(rgba8, binding = 0) writeonly uniform image2D canvasColors;
layout(r32i, binding = 1) uniform iimage2D canvasDistances;
layout(rgba8, binding = 2) readonly uniform image2D imageAtlas;

// Most taps an anisotropic footprint spreads along its longer axis.
const int kImageMaxTaps = 4;

ivec2 mipLevelSize(ivec2 baseSize, int level) {
    return max(ivec2(baseSize.x >> level, baseSize.y >> level), ivec2(1));
}

// Level 0 sits at the atlas origin; every smaller level stacks top to bottom
// in a column at x = baseSize.x.
ivec2 mipAtlasLevelOrigin(ivec2 baseSize, int level) {
    if (level <= 0) {
        return ivec2(0);
    }
    int y = 0;
    for (int above = 1; above < level; above++) {
        y += mipLevelSize(baseSize, above).y;
    }
    return ivec2(baseSize.x, y);
}

vec4 imageBilinear(ivec2 baseSize, int level, vec2 uv) {
    ivec2 levelSize = mipLevelSize(baseSize, level);
    ivec2 levelOrigin = mipAtlasLevelOrigin(baseSize, level);
    vec2 texel = uv * vec2(levelSize) - 0.5;
    vec2 texelFloor = floor(texel);
    vec2 blend = texel - texelFloor;
    ivec2 last = levelSize - ivec2(1);
    ivec2 lo = clamp(ivec2(texelFloor), ivec2(0), last);
    ivec2 hi = clamp(ivec2(texelFloor) + ivec2(1), ivec2(0), last);
    vec4 topLeft = imageLoad(imageAtlas, levelOrigin + lo);
    vec4 topRight = imageLoad(imageAtlas, levelOrigin + ivec2(hi.x, lo.y));
    vec4 bottomLeft = imageLoad(imageAtlas, levelOrigin + ivec2(lo.x, hi.y));
    vec4 bottomRight = imageLoad(imageAtlas, levelOrigin + hi);
    return mix(mix(topLeft, topRight, blend.x), mix(bottomLeft, bottomRight, blend.x), blend.y);
}

vec4 imageTrilinear(ivec2 baseSize, int levelCount, vec2 uv, float lod) {
    int lastLevel = levelCount - 1;
    int fine = clamp(int(floor(lod)), 0, lastLevel);
    int coarse = min(fine + 1, lastLevel);
    vec4 fineColor = imageBilinear(baseSize, fine, uv);
    if (coarse == fine) {
        return fineColor;
    }
    vec4 coarseColor = imageBilinear(baseSize, coarse, uv);
    return mix(fineColor, coarseColor, clamp(lod - float(fine), 0.0, 1.0));
}

void main() {
    CanvasImageCommand cmd = command;
    ivec2 pixel = cmd.clipLo + ivec2(gl_GlobalInvocationID.xy);
    ivec2 canvasSize = imageSize(canvasColors);
    if (pixel.x >= cmd.clipHi.x || pixel.y >= cmd.clipHi.y ||
        !isInsideCanvas(pixel, canvasSize)) {
        return;
    }

    // Every texel of a block samples the block's centre, so the block is one
    // colour.
    int cell = max(cmd.cell, 1);
    ivec2 block = (pixel - cmd.origin) / cell;
    vec2 extent = vec2(cmd.size);
    vec2 blockCenter = min((vec2(block) + 0.5) * float(cell), extent);
    vec2 uvSpan = cmd.uvRect.zw - cmd.uvRect.xy;
    vec2 uv = clamp(cmd.uvRect.xy + blockCenter / extent * uvSpan, 0.0, 1.0);

    // The block's footprint in level-0 texels. The level is picked for the
    // shorter side, and the longer side is covered by taps spread along it.
    vec2 footprint = abs(uvSpan) * vec2(cmd.baseSize) * float(cell) / extent;
    bool wide = footprint.x >= footprint.y;
    float longSide = max(footprint.x, footprint.y);
    float shortSide = max(min(footprint.x, footprint.y), 1.0);
    int taps = clamp(int(ceil(longSide / shortSide)), 1, kImageMaxTaps);
    float lod = log2(max(longSide / float(taps), 1.0));
    vec2 longAxisUv = (wide ? vec2(1.0, 0.0) : vec2(0.0, 1.0)) * longSide / vec2(cmd.baseSize);
    vec4 color = vec4(0.0);
    for (int tap = 0; tap < taps; tap++) {
        float along = (float(tap) + 0.5) / float(taps) - 0.5;
        vec2 tapUv = clamp(uv + longAxisUv * along, 0.0, 1.0);
        color += imageTrilinear(cmd.baseSize, cmd.levelCount, tapUv, lod);
    }
    color /= float(taps);

    // The atlas is premultiplied; a mostly transparent texel is left alone.
    if (color.a < 0.5) {
        return;
    }
    vec3 rgb = clamp(color.rgb / color.a, 0.0, 1.0);
    if (cmd.posterizeLevels >= 2) {
        float steps = float(cmd.posterizeLevels - 1);
        rgb = floor(rgb * steps + 0.5) / steps;
    }
    rgb *= unpackColor(cmd.tintPacked).rgb;

    imageStore(canvasColors, pixel, vec4(rgb, 1.0));
    imageStore(canvasDistances, pixel, ivec4(cmd.distance));
}
