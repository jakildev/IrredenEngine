#include "ir_iso_common.metal"

// One image draw onto a trixel canvas. Mirrors
// shaders/c_canvas_image_to_trixel.glsl.
//
// Workgroup dimensions: (8, 8, 1), one thread per canvas texel of the draw's
// clipped box, dispatched from `clipLo`.

struct CanvasImageCommand {
    int2 clipLo;
    int2 clipHi;
    int2 origin;
    int2 size;
    float4 uvRect;         // u0, v0, u1, v1
    int2 baseSize;         // level 0 of the atlas
    int levelCount;
    int cell;
    uint tintPacked;
    int posterizeLevels;   // 0 = off
    int distance;
    int pad;
};

// Most taps an anisotropic footprint spreads along its longer axis.
constant int kImageMaxTaps = 4;

inline int2 mipLevelSize(int2 baseSize, int level) {
    return max(int2(baseSize.x >> level, baseSize.y >> level), int2(1));
}

// Level 0 sits at the atlas origin; every smaller level stacks top to bottom
// in a column at x = baseSize.x.
inline int2 mipAtlasLevelOrigin(int2 baseSize, int level) {
    if (level <= 0) {
        return int2(0);
    }
    int y = 0;
    for (int above = 1; above < level; ++above) {
        y += mipLevelSize(baseSize, above).y;
    }
    return int2(baseSize.x, y);
}

inline float4 imageBilinear(
    texture2d<float, access::read> imageAtlas, int2 baseSize, int level, float2 uv
) {
    const int2 levelSize = mipLevelSize(baseSize, level);
    const int2 levelOrigin = mipAtlasLevelOrigin(baseSize, level);
    const float2 texel = uv * float2(levelSize) - 0.5;
    const float2 texelFloor = floor(texel);
    const float2 blend = texel - texelFloor;
    const int2 last = levelSize - int2(1);
    const int2 lo = clamp(int2(texelFloor), int2(0), last);
    const int2 hi = clamp(int2(texelFloor) + int2(1), int2(0), last);
    const float4 topLeft = imageAtlas.read(uint2(levelOrigin + lo));
    const float4 topRight = imageAtlas.read(uint2(levelOrigin + int2(hi.x, lo.y)));
    const float4 bottomLeft = imageAtlas.read(uint2(levelOrigin + int2(lo.x, hi.y)));
    const float4 bottomRight = imageAtlas.read(uint2(levelOrigin + hi));
    return mix(mix(topLeft, topRight, blend.x), mix(bottomLeft, bottomRight, blend.x), blend.y);
}

inline float4 imageTrilinear(
    texture2d<float, access::read> imageAtlas, int2 baseSize, int levelCount, float2 uv, float lod
) {
    const int lastLevel = levelCount - 1;
    const int fine = clamp(int(floor(lod)), 0, lastLevel);
    const int coarse = min(fine + 1, lastLevel);
    const float4 fineColor = imageBilinear(imageAtlas, baseSize, fine, uv);
    if (coarse == fine) {
        return fineColor;
    }
    const float4 coarseColor = imageBilinear(imageAtlas, baseSize, coarse, uv);
    return mix(fineColor, coarseColor, clamp(lod - float(fine), 0.0, 1.0));
}

kernel void c_canvas_image_to_trixel(
    device const CanvasImageCommand* commands [[buffer(12)]],
    texture2d<float, access::write> canvasColors [[texture(0)]],
    texture2d<int, access::write> canvasDistances [[texture(1)]],
    texture2d<float, access::read> imageAtlas [[texture(2)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const CanvasImageCommand cmd = commands[0];
    const int2 pixel = cmd.clipLo + int2(globalId.xy);
    const int2 canvasSize = int2(
        int(canvasColors.get_width()),
        int(canvasColors.get_height())
    );
    if (pixel.x >= cmd.clipHi.x || pixel.y >= cmd.clipHi.y ||
        !isInsideCanvas(pixel, canvasSize)) {
        return;
    }

    // Every texel of a block samples the block's centre, so the block is one
    // colour.
    const int cell = max(cmd.cell, 1);
    const int2 block = (pixel - cmd.origin) / cell;
    const float2 extent = float2(cmd.size);
    const float2 blockCenter = min((float2(block) + 0.5) * float(cell), extent);
    const float2 uvSpan = cmd.uvRect.zw - cmd.uvRect.xy;
    const float2 uv = clamp(cmd.uvRect.xy + blockCenter / extent * uvSpan, 0.0, 1.0);

    // The block's footprint in level-0 texels. The level is picked for the
    // shorter side, and the longer side is covered by taps spread along it.
    const float2 footprint = abs(uvSpan) * float2(cmd.baseSize) * float(cell) / extent;
    const bool wide = footprint.x >= footprint.y;
    const float longSide = max(footprint.x, footprint.y);
    const float shortSide = max(min(footprint.x, footprint.y), 1.0);
    const int taps = clamp(int(ceil(longSide / shortSide)), 1, kImageMaxTaps);
    const float lod = log2(max(longSide / float(taps), 1.0));
    const float2 longAxisUv =
        (wide ? float2(1.0, 0.0) : float2(0.0, 1.0)) * longSide / float2(cmd.baseSize);
    float4 color = float4(0.0);
    for (int tap = 0; tap < taps; ++tap) {
        const float along = (float(tap) + 0.5) / float(taps) - 0.5;
        const float2 tapUv = clamp(uv + longAxisUv * along, 0.0, 1.0);
        color += imageTrilinear(imageAtlas, cmd.baseSize, cmd.levelCount, tapUv, lod);
    }
    color /= float(taps);

    // The atlas is premultiplied; a mostly transparent texel is left alone.
    if (color.a < 0.5) {
        return;
    }
    float3 rgb = clamp(color.rgb / color.a, 0.0, 1.0);
    if (cmd.posterizeLevels >= 2) {
        const float steps = float(cmd.posterizeLevels - 1);
        rgb = floor(rgb * steps + 0.5) / steps;
    }
    rgb *= unpackColor(cmd.tintPacked).rgb;

    canvasColors.write(float4(rgb, 1.0), uint2(pixel));
    canvasDistances.write(int4(cmd.distance, 0, 0, 0), uint2(pixel));
}
