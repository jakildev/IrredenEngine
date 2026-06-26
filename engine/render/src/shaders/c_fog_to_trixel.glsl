#version 450 core

// Screen-space fog-of-war pass. Runs after LIGHTING_TO_TRIXEL and before
// TRIXEL_TO_TRIXEL. For each rasterized pixel, recovers the source voxel's
// world (x,y) column from the encoded distance + iso pixel coords, samples
// the 2D fog visibility texture at that column with manual 4-tap bilinear,
// and modulates the canvas color continuously by the sampled state:
//   visible    (1.0)     — pass through
//   explored   (128/255) — desaturate + darken (fog-of-war "memory")
//   unexplored (0.0)     — black
// with a smooth lerp between those anchors so a feathered reveal edge (and a
// sub-cell-moving observer) fades without per-cell vibration. The image
// binding has no hardware sampler, so the bilinear is done by hand; OOB taps
// read as visible (the wrap-mode invariant in the component header). Over a
// uniform region the bilinear collapses to the exact cell value and the lerp
// passes through the old bucket output, so a fully-revealed scene is
// byte-identical to the pre-feather pass.
//
// Background pixels (no rasterized geometry) keep their cleared color so
// the engine's default clear paints around the visible region — adding a
// world-grounded fog tint to background pixels would require sampling the
// fog along the camera ray, which v1 defers.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

// Mirrors C_CanvasFogOfWar in
// engine/prefabs/irreden/render/components/component_canvas_fog_of_war.hpp.
// World-space fog grid is centered on origin with half-extent = size / 2.
const int kFogOfWarSize = 256;
const int kFogOfWarHalfExtent = 128;

// Same threshold LIGHTING_TO_TRIXEL uses for "empty pixel" — encoded
// distances >= 65535 mean the clear value was never overwritten.
const int kEmptyDistanceEncoded = 65535;

// Normalized stored explored value (128/255, NOT 0.5). The continuous
// modulation below pivots through this so the two-segment lerp passes exactly
// through the pre-feather bucket outputs at the three canonical stored states
// (0 / 128 / 255): uniform-region pixels stay byte-identical, and only a
// fractional (feathered or sub-cell-sampled) value bends the curve.
const float kFogExploredValue = 128.0 / 255.0;

// Mirrors FrameDataVoxelToTrixel; we only need frameCanvasOffset,
// trixelCanvasOffsetZ1, and voxelRenderOptions for the pixel→pos3D
// reconstruction, but std140 layout requires the full block.
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
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;
};

layout(rgba8, binding = 0) uniform image2D trixelColors;
layout(r32i, binding = 1) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 2) readonly uniform image2D canvasFogOfWar;

// One bilinear tap of the fog grid. Out-of-range cells read as visible (1.0):
// imageLoad has no sampler wrap mode, so this bounds check is load-bearing
// (the OOB-as-visible invariant in the component header).
float fogTap(ivec2 cell, ivec2 fogSize) {
    if (cell.x < 0 || cell.x >= fogSize.x ||
        cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0;
    }
    return imageLoad(canvasFogOfWar, cell).r;
}

void main() {
    const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size = imageSize(trixelColors);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    // Background pixels: no geometry, no associated world cell — leave
    // alone. (See header comment for the deferred camera-ray approach.)
    const int encoded = imageLoad(trixelDistances, pixel).x;
    if (encoded >= kEmptyDistanceEncoded) {
        return;
    }

    // Recover world-space pos3D from the iso pixel + raw depth, applying
    // the same subdivision-aware scaling that c_compute_voxel_ao.glsl
    // uses. The three pos3D-recovery shaders (AO, sun shadow, fog) must
    // stay in lockstep with the stage-2 encoding. R(-rasterYaw) recovers
    // world coords from the cardinal-rotated raster frame; at
    // cardinalIndex==0 the path collapses to master so yaw=0 stays
    // byte-identical.
    const int rawDepth = encoded >> 2;
    vec3 pos3D = trixelCanvasPixelToWorld3D(
        pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
    );

    // Manual 4-tap bilinear over the continuous world column. Iso convention:
    // X-Y is the floor plane, so the fog grid lookup is (x, y) with the
    // half-extent offset; +Z is the downward height axis and plays no part.
    // Sampling the float column (not a rounded cell) is what feathers a
    // sub-cell-moving reveal edge — at an integer column frac==0 and the tap
    // collapses to the exact cell.
    const vec2 fogCoord = pos3D.xy + vec2(float(kFogOfWarHalfExtent));
    const vec2 base = floor(fogCoord);
    const vec2 frac = fogCoord - base;
    const ivec2 cell = ivec2(base);
    const ivec2 fogSize = imageSize(canvasFogOfWar);
    const float t00 = fogTap(cell + ivec2(0, 0), fogSize);
    const float t10 = fogTap(cell + ivec2(1, 0), fogSize);
    const float t01 = fogTap(cell + ivec2(0, 1), fogSize);
    const float t11 = fogTap(cell + ivec2(1, 1), fogSize);
    const float state = mix(mix(t00, t10, frac.x), mix(t01, t11, frac.x), frac.y);

    // Fully-visible fast path: pass through untouched. Byte-identical to the
    // pre-feather pass over a fully-revealed region, and skips the store.
    if (state >= 1.0) {
        return;
    }

    const vec4 src = imageLoad(trixelColors, pixel);
    // Desaturate to luminance, then darken — the explored "memory" tone. Keeps
    // shape silhouettes visible so the player remembers what was there without
    // confusing it with what is *currently* there.
    const float luminance = dot(src.rgb, vec3(0.299, 0.587, 0.114));
    const vec3 exploredColor = vec3(luminance) * 0.4;

    // Two-segment continuous lerp anchored on the three canonical stored
    // states: black at 0, exploredColor at 128/255, src at 1.0. Alpha is
    // preserved so any text/overlay antialiasing still composites cleanly.
    vec3 outColor;
    if (state >= kFogExploredValue) {
        const float t = (state - kFogExploredValue) / (1.0 - kFogExploredValue);
        outColor = mix(exploredColor, src.rgb, t);
    } else {
        const float t = state / kFogExploredValue;
        outColor = mix(vec3(0.0), exploredColor, t);
    }
    imageStore(trixelColors, pixel, vec4(outColor, src.a));
}
