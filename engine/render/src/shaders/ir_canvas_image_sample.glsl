#include "ir_iso_common.glsl"
#include "ir_canvas_image_command.glsl"

// One image draw's sampling, shared by the canvas kernel
// (c_canvas_image_to_trixel) and the framebuffer pass (f_image_to_framebuffer).
// A pixel is a texel of whichever target the draw's box is measured in.
//
// The includer binds the draw's MipAtlas its own way (an image unit in the
// kernel, a texture unit in the fragment stage) and defines, before this
// include, the one read of it:
//   vec4 imageAtlasTexel(ivec2 texel);

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
    vec4 topLeft = imageAtlasTexel(levelOrigin + lo);
    vec4 topRight = imageAtlasTexel(levelOrigin + ivec2(hi.x, lo.y));
    vec4 bottomLeft = imageAtlasTexel(levelOrigin + ivec2(lo.x, hi.y));
    vec4 bottomRight = imageAtlasTexel(levelOrigin + hi);
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

// The colour the draw gives `pixel`, or false where the target keeps what it
// held: outside the draw's turned box, and where the source is mostly
// transparent.
bool canvasImageColor(CanvasImageCommand cmd, ivec2 pixel, out vec3 rgb) {
    // The pixel centre in the unturned box, in texels from its top-left.
    // Every term is a multiple of one half, so an axis-aligned draw lands on
    // pixel - origin + 0.5 exactly.
    vec2 extent = vec2(cmd.size);
    vec2 fromCenter = vec2(pixel) + 0.5 - (vec2(cmd.origin) + 0.5 * extent);
    vec2 local = vec2(dot(cmd.unrotate.xy, fromCenter), dot(cmd.unrotate.zw, fromCenter)) +
        0.5 * extent;
    if (local.x < 0.0 || local.y < 0.0 || local.x >= extent.x || local.y >= extent.y) {
        return false;
    }

    // Every texel of a mosaic block samples the block's centre, so the block
    // is one colour and turns with the box. Without a mosaic a texel samples
    // under its own centre and covers the box texels one target texel spans.
    int cell = max(cmd.cell, 1);
    vec2 samplePoint = local;
    vec2 sampleSpan = abs(cmd.unrotate.xz) + abs(cmd.unrotate.yw);
    if (cell > 1) {
        ivec2 block = ivec2(local) / cell;
        samplePoint = min((vec2(block) + 0.5) * float(cell), extent);
        sampleSpan = vec2(float(cell));
    }
    vec2 uvSpan = cmd.uvRect.zw - cmd.uvRect.xy;
    vec2 uv = clamp(cmd.uvRect.xy + samplePoint / extent * uvSpan, 0.0, 1.0);

    // The sample's footprint in level-0 texels. The level is picked for the
    // shorter side, and the longer side is covered by taps spread along it.
    vec2 footprint = abs(uvSpan) * vec2(cmd.baseSize) * sampleSpan / extent;
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

    // The atlas is premultiplied.
    if (color.a < 0.5) {
        return false;
    }
    rgb = clamp(color.rgb / color.a, 0.0, 1.0);
    if (cmd.posterizeLevels >= 2) {
        float steps = float(cmd.posterizeLevels - 1);
        rgb = floor(rgb * steps + 0.5) / steps;
    }
    rgb *= unpackColor(cmd.tintPacked).rgb;
    return true;
}
