#include "ir_iso_common.metal"

// Mirrors shaders/c_fog_to_trixel.glsl. Screen-space fog-of-war pass —
// recovers each rasterized pixel's source-voxel column, then masks trixelColors
// by the MAX of two reveal sources:
//   1. The voxel GRID — a single NEAREST read of the fog texture at the
//      rounded cell. Coarse, voxel-quantized: explored/voxelized memory.
//   2. Live analytic VISION CIRCLES — world-space discs (FogObserverData)
//      tested against the CONTINUOUS world column `pos3D.xy` per pixel, so a
//      disc edge is crisp at render resolution, slides smoothly with sub-voxel
//      observer motion, and reveals partial voxels at the boundary.
// The combined visibility drives a continuous two-segment lerp:
//   visible    (1.0)     — pass through
//   explored   (128/255) — desaturate + darken
//   unexplored (0.0)     — black
// Texture reads have no hardware sampler, so OOB grid cells read as visible.
// With no vision circles (count 0) the pass is grid-only — byte-identical to
// the legacy bucket pass at the canonical 0/128/255 stored states.

constant int kFogOfWarSize = 256;
constant int kFogOfWarHalfExtent = 128;
constant int kEmptyDistanceEncoded = 65535;
// Normalized stored explored value (128/255, NOT 0.5) — the continuous
// modulation pivots through this so the two-segment lerp passes exactly
// through the legacy bucket outputs at the three canonical stored states.
constant float kFogExploredValue = 128.0f / 255.0f;

// Live analytic vision circles. Mirrors kMaxFogVisionCircles and
// FrameDataFogObservers in component_canvas_fog_of_war.hpp — this struct is the
// system's `FogObserverData` upload verbatim. visionCircles[i] =
// (centerX, centerY, radius, edgeSoftness) in world units; only the first
// visionCircleCount entries are read.
constant int kMaxFogVisionCircles = 8;
struct FogObserverData {
    float4 visionCircles[kMaxFogVisionCircles];
    int visionCircleCount;
    // Reserved padding lane. Previously carried the light-occlusion-grid
    // availability flag for the retired ray+occupancy cut variant; the
    // geometric cross-section cap needs no external occupancy source, so
    // the lane is unread (kept for the std140/Metal layout).
    int _fogObserverPad0;
};

// Cross-section cap tuning — mirrors the GLSL twin (see there for the full
// rationale: vertical faces only, radial band, no ray / occupancy reads).
constant float kFogCutTone = 0.85f;
constant float kFogCutMaxRimCells = 2.0f;
// Rim fade — mirrors the GLSL twin (see there for the full rationale and the
// kFogHiddenKeepCells width coupling).
constant float kFogRimFadeCells = 8.0f;
constant float kFogRimFadeLevel = 0.75f;

// Read the fog grid at a cell. Out-of-range cells read as visible (1.0):
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
    // buffer(27) ALIASES kBufferIndex_FrameDataLightingToTrixel — the Metal
    // 0-30 buffer table is full, and fog runs right after lighting (done with
    // slot 27 by then). See kBufferIndex_FogObservers in ir_render_types.hpp.
    constant FogObserverData& fogObservers [[buffer(27)]],
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

    // Grid memory: a single NEAREST read at the rounded cell. Iso convention:
    // X-Y is the floor plane, so the fog grid lookup is (x, y) with the
    // half-extent offset; +Z is the downward height axis and plays no part.
    const int3 surfaceVoxel = roundHalfUp(pos3D);
    const int2 fogCell = surfaceVoxel.xy + int2(kFogOfWarHalfExtent);
    const int2 fogSize = int2(
        int(canvasFogOfWar.get_width()),
        int(canvasFogOfWar.get_height())
    );
    // Kept separate from the circle-combined `state`: the cross-section band
    // below applies only to UNEXPLORED surfaces (explored memory keeps its
    // desaturated tone).
    const float gridState = fogTap(fogCell, fogSize, canvasFogOfWar);
    float state = gridState;
    // This column's world distance PAST the nearest hard disc's radius —
    // drives the cross-section cap band and the rim fade below. Initialized
    // to the full fade width so no-hard-disc scenes (including soft Mode B
    // discs) resolve to cap-off + fade 0 (legacy path).
    float hardDistPastRim = kFogRimFadeCells;

    // Live analytic vision circles: max-combine each disc's smooth visibility,
    // evaluated against the CONTINUOUS world column so the edge is crisp at
    // render resolution and reveals partial voxels as a moving observer slides
    // sub-cell. The rim is floored at one canvas pixel for zoom-stable AA; at
    // count 0 the loop is skipped and `state` stays the grid value (legacy).
    if (fogObservers.visionCircleCount > 0) {
        // Local world-units-per-pixel from the iso inverse-projection Jacobian:
        // recover the +x neighbour at the same depth and measure the world step.
        // Zoom- and iso-correct, so the AA rim stays ~1px at any zoom with no
        // uniform.
        const float3 pos3DNeighborX = trixelCanvasPixelToWorld3D(
            pixel + int2(1, 0),
            rawDepth,
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions,
            frameData.rasterYaw
        );
        const float worldPerPixel = length(pos3DNeighborX.xy - pos3D.xy);
        // Shared with c_voxel_to_trixel_stage_1's per-voxel clip so the floor's
        // per-pixel reveal here and the voxel-object edge there trace the same
        // analytic curve (#2102). worldPerPixel floors the rim at ~1 canvas px.
        for (int i = 0; i < fogObservers.visionCircleCount; ++i) {
            const float reveal =
                fogVisionCircleReveal(pos3D.xy, fogObservers.visionCircles[i], worldPerPixel);
            state = max(state, reveal);
            if (fogObservers.visionCircles[i].w == 0.0f) {
                hardDistPastRim = min(
                    hardDistPastRim,
                    length(pos3D.xy - fogObservers.visionCircles[i].xy) -
                        fogObservers.visionCircles[i].z
                );
            }
        }
    }

    // Fully-visible fast path: pass through untouched. Skips the store.
    if (state >= 1.0f) {
        return;
    }

    const float4 src = trixelColors.read(uint2(pixel));

    // Cross-section cap (#2124) — mirrors the GLSL twin exactly (see there
    // for the full rationale): VERTICAL faces (the depth encoding's slot bits
    // resolved through visibleFaceIds) blend the cut tint over the fade with
    // a weight that is 1 at the rim and 0 at kFogCutMaxRimCells, so the wall
    // is one continuous curve converging to the top face's exact fade tone at
    // the band's end — no hard band-edge line, no voxel-stepped teeth. TOP
    // (Z) faces never cap (flat ground fades on the plain radial curve).
    // Colour-only, after lighting — never touches trixelDistances.

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
    if (gridState < kFogExploredValue) {
        // Rim fade (see the const block): lift UNEXPLORED pixels toward their
        // lit colour near the hard-disc rim. Explored memory keeps its tone.
        // The squared ease-out crushes the fade tail to black well before the
        // keep-ring drop, so the outermost kept columns' wall faces (whose
        // constant-depth recovery reads a column slightly INSIDE their true
        // one) can't catch a visible lift against the void behind them.
        const float u = 1.0f - smoothstep(0.0f, kFogRimFadeCells, hardDistPastRim);
        outColor = mix(outColor, src.rgb, kFogRimFadeLevel * u * u);
        // Feathered cross-section cap on vertical faces (see the block
        // comment above). `state` carries the disc's ~1px AA rim, so the
        // junction with visible matter stays antialiased.
        const int faceAxis = frameData.visibleFaceIds[encoded & 3] >> 1;
        if (fogObservers.visionCircleCount > 0 && faceAxis != 2) {
            const float capBlend =
                1.0f - smoothstep(0.0f, kFogCutMaxRimCells, max(hardDistPastRim, 0.0f));
            const float3 capColor = mix(src.rgb * kFogCutTone, src.rgb, state);
            outColor = mix(outColor, capColor, capBlend);
        }
    }
    trixelColors.write(float4(outColor, src.a), uint2(pixel));
}
