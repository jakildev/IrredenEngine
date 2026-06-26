#include "ir_iso_common.metal"

// Mirrors shaders/c_fog_to_trixel.glsl. Screen-space fog-of-war pass —
// recovers each rasterized pixel's source-voxel column, samples the 2D fog
// visibility texture at that (x,y) column with manual 4-tap bilinear, and
// modulates trixelColors continuously by the sampled state:
//   visible    (1.0)     — pass through
//   explored   (128/255) — desaturate + darken
//   unexplored (0.0)     — black
// with a smooth lerp between those anchors so a feathered reveal edge (and a
// sub-cell-moving observer) fades without per-cell vibration. Texture reads
// have no hardware sampler, so the bilinear is by hand; OOB taps read as
// visible. Over a uniform region the bilinear collapses to the exact cell and
// the lerp passes through the old bucket output, so a fully-revealed scene is
// byte-identical to the pre-feather pass.

constant int kFogOfWarSize = 256;
constant int kFogOfWarHalfExtent = 128;
constant int kEmptyDistanceEncoded = 65535;
// Normalized stored explored value (128/255, NOT 0.5) — the continuous
// modulation pivots through this so the two-segment lerp passes exactly
// through the pre-feather bucket outputs at the three canonical stored states.
constant float kFogExploredValue = 128.0f / 255.0f;

// One bilinear tap of the fog grid. Out-of-range cells read as visible (1.0):
// texture reads have no sampler wrap mode, so this bounds check is load-bearing
// (the OOB-as-visible invariant in the component header).
static float fogTap(int2 cell, int2 fogSize, texture2d<float, access::read> fog) {
    if (cell.x < 0 || cell.x >= fogSize.x ||
        cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0f;
    }
    return fog.read(uint2(cell)).r;
}

kernel void c_fog_to_trixel(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    texture2d<float, access::read_write> trixelColors [[texture(0)]],
    texture2d<int, access::read> trixelDistances [[texture(1)]],
    texture2d<float, access::read> canvasFogOfWar [[texture(2)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const int2 pixel = int2(globalId.xy);
    const int2 size = int2(
        int(trixelColors.get_width()),
        int(trixelColors.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    const int encoded = trixelDistances.read(uint2(pixel)).x;
    if (encoded >= kEmptyDistanceEncoded) {
        return;
    }

    // R(-rasterYaw) recovers world coords from the cardinal-rotated raster
    // frame; the fog grid is world-space (X-Y plane). At cardinalIndex==0
    // the path collapses to master so yaw=0 stays byte-identical.
    const int rawDepth = encoded >> 2;
    float3 pos3D = trixelCanvasPixelToWorld3D(
        pixel,
        rawDepth,
        frameData.trixelCanvasOffsetZ1,
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions,
        frameData.rasterYaw
    );

    // Manual 4-tap bilinear over the continuous world column. Iso convention:
    // X-Y is the floor plane, so the fog grid lookup is (x, y) with the
    // half-extent offset; +Z is the downward height axis and plays no part.
    // Sampling the float column (not a rounded cell) is what feathers a
    // sub-cell-moving reveal edge — at an integer column frac==0 and the tap
    // collapses to the exact cell.
    const float2 fogCoord = pos3D.xy + float2(float(kFogOfWarHalfExtent));
    const float2 base = floor(fogCoord);
    const float2 frac = fogCoord - base;
    const int2 cell = int2(base);
    const int2 fogSize = int2(
        int(canvasFogOfWar.get_width()),
        int(canvasFogOfWar.get_height())
    );
    const float t00 = fogTap(cell + int2(0, 0), fogSize, canvasFogOfWar);
    const float t10 = fogTap(cell + int2(1, 0), fogSize, canvasFogOfWar);
    const float t01 = fogTap(cell + int2(0, 1), fogSize, canvasFogOfWar);
    const float t11 = fogTap(cell + int2(1, 1), fogSize, canvasFogOfWar);
    const float state = mix(mix(t00, t10, frac.x), mix(t01, t11, frac.x), frac.y);

    // Fully-visible fast path: pass through untouched. Byte-identical to the
    // pre-feather pass over a fully-revealed region, and skips the store.
    if (state >= 1.0f) {
        return;
    }

    const float4 src = trixelColors.read(uint2(pixel));
    // Desaturate to luminance, then darken — the explored "memory" tone.
    const float luminance = dot(src.rgb, float3(0.299f, 0.587f, 0.114f));
    const float3 exploredColor = float3(luminance) * 0.4f;

    // Two-segment continuous lerp anchored on the three canonical stored
    // states: black at 0, exploredColor at 128/255, src at 1.0. Alpha is
    // preserved so any text/overlay antialiasing still composites cleanly.
    float3 outColor;
    if (state >= kFogExploredValue) {
        const float t = (state - kFogExploredValue) / (1.0f - kFogExploredValue);
        outColor = mix(exploredColor, src.rgb, t);
    } else {
        const float t = state / kFogExploredValue;
        outColor = mix(float3(0.0f), exploredColor, t);
    }
    trixelColors.write(float4(outColor, src.a), uint2(pixel));
}
