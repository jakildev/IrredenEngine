#version 450 core

// Screen-space fog-of-war pass. Runs after LIGHTING_TO_TRIXEL and before
// TRIXEL_TO_TRIXEL. For each rasterized pixel, recovers the source voxel's
// world (x,y) column from the encoded distance + iso pixel coords, then masks
// the canvas color by the MAX of two reveal sources:
//   1. The voxel GRID — a single NEAREST read of the fog texture at the
//      rounded cell. Coarse, voxel-quantized: explored/voxelized memory.
//   2. Live analytic VISION CIRCLES — world-space discs (FogObserverData)
//      evaluated against the CONTINUOUS world column `pos3D.xy`. Because the
//      distance test runs per pixel (render resolution, not per cell), a disc
//      edge is crisp and slides smoothly with sub-voxel observer motion, and a
//      voxel straddling the boundary is partially revealed (some pixels inside,
//      some out) — the smooth reveal the grid cannot express.
// The combined visibility then drives a continuous modulation:
//   visible    (1.0)     — pass through
//   explored   (128/255) — desaturate + darken (fog-of-war "memory")
//   unexplored (0.0)     — black
// with a smooth two-segment lerp between those anchors. The grid read uses an
// image binding with no hardware sampler, so OOB cells read as visible (the
// wrap-mode invariant in the component header). With no vision circles
// (count 0) the pass is grid-only — byte-identical to the legacy bucket pass
// at the canonical 0/128/255 stored states.
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
// through the legacy bucket outputs at the three canonical stored states
// (0 / 128 / 255): grid-only pixels stay byte-identical, and only a fractional
// (analytic vision-circle) value bends the curve.
const float kFogExploredValue = 128.0 / 255.0;

// Live analytic vision circles. Mirrors kMaxFogVisionCircles and
// FrameDataFogObservers in component_canvas_fog_of_war.hpp — the std140 block
// is the system's `FogObserverData` upload verbatim. visionCircles[i] =
// (centerX, centerY, radius, edgeSoftness) in world units; only the first
// visionCircleCount entries are read.
// binding 27 ALIASES kBufferIndex_FrameDataLightingToTrixel — the Metal 0-30
// buffer table is full, and fog runs right after lighting (which has finished
// with slot 27 by then). See kBufferIndex_FogObservers in ir_render_types.hpp.
const int kMaxFogVisionCircles = 8;
layout(std140, binding = 27) uniform FogObserverData {
    vec4 visionCircles[kMaxFogVisionCircles];
    int visionCircleCount;
    // Reserved padding lane. Previously carried the light-occlusion-grid
    // availability flag for the retired ray+occupancy cut variant; the
    // geometric cross-section cap below needs no external occupancy source,
    // so the lane is unread (kept for the std140/Metal layout).
    int _fogObserverPad0;
};

// Cross-section cap tuning. The tone is the factor applied to the hidden
// surface's lit colour where the cap tints it, so the cross-section reads as
// the object's inside rather than its surface; the lift is a small constant
// floor added on top so a cut through DARK matter (a near-black floor) still
// reads against the fog black instead of vanishing — hue-preserving for
// bright materials, a subtle gray-up for dark ones.
const float kFogCutTone = 0.85;
const float kFogCutLift = 0.06;
// The cap is a boundary CAP, not a reveal: only VERTICAL faces whose OWN
// world column sits within this many world units PAST the disc radius are
// tinted; beyond it the rim fade owns the falloff. The cap metric is RADIAL
// surface-XY distance past the rim — the same metric the reveal and the rim
// fade key on — so every fog boundary is a circle concentric with the disc
// at every face height. (Capping by ray-entry distance instead sweeps the
// disc along the view ray's world-(1,1) XY — a pure screen-vertical
// displacement in iso — so that variant's band edge read screen-space-ish
// on elevated faces.) VERTICAL surfaces far past the radius (an arena
// slab's side, border rails) fail the same radial test and stay on the
// fade; TOP faces are excluded entirely at the cap site below.
const float kFogCutMaxRimCells = 2.0;

// Rim fade — the fallback for hidden RASTERIZED matter the cut does not
// repaint. Instead of dropping straight to the unexplored black, an
// unexplored pixel's dark tone is lifted toward its lit colour by a factor
// that starts at kFogRimFadeLevel at the disc rim and decays to zero over
// kFogRimFadeCells of column distance past the radius. Matter the occupancy
// test misses — air pockets at an object's cut corner, sub-cell-thin floors,
// hollow interiors — lands in this smooth gradient instead of a black band,
// so the boundary stays artifact-free for ANY content the raster kept.
// kFogRimFadeCells MUST equal stage-1's kFogHiddenKeepCells: the fade
// reaches black exactly where hidden columns stop rasterizing, so the
// keep-ring's outer drop edge never shows as a visible step. The level sits
// just under kFogCutTone so the solid-backed cut band still reads brighter
// than the fade around it (the cross-section face keeps its identity).
// Hard discs only — a soft (Mode B) disc's wide falloff IS its fade.
const float kFogRimFadeCells = 8.0;
const float kFogRimFadeLevel = 0.75;

// Mirrors FrameDataVoxelToTrixel; we need frameCanvasOffset,
// trixelCanvasOffsetZ1, and voxelRenderOptions for the pixel→pos3D
// reconstruction, plus visibleFaceIds so the cross-section cap below can
// resolve each pixel's rasterized face axis from the depth encoding's slot
// bits (the same slot→faceId mapping AO and lighting use). std140 layout
// must match the producer declaration in c_voxel_to_trixel_stage_1.glsl.
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
    uniform vec4 faceDeform[3];
    uniform ivec4 visibleFaceIds;
};

layout(rgba8, binding = 0) uniform image2D trixelColors;
layout(r32i, binding = 1) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 2) readonly uniform image2D canvasFogOfWar;

// Read the fog grid at a cell. Out-of-range cells read as visible (1.0):
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

    // Grid memory: a single NEAREST read at the rounded cell. Iso convention:
    // X-Y is the floor plane, so the fog grid lookup is (x, y) with the
    // half-extent offset; +Z is the downward height axis and plays no part.
    const ivec3 surfaceVoxel = roundHalfUp(pos3D);
    const ivec2 fogCell = surfaceVoxel.xy + ivec2(kFogOfWarHalfExtent);
    const ivec2 fogSize = imageSize(canvasFogOfWar);
    // Kept separate from the circle-combined `state`: the cross-section band
    // below applies only to UNEXPLORED surfaces (explored memory keeps its
    // desaturated tone).
    const float gridState = fogTap(fogCell, fogSize);
    float state = gridState;
    // This column's world distance PAST the nearest hard disc's radius —
    // drives the cross-section cap band and the rim fade below. Initialized
    // to the full fade width so no-hard-disc scenes (including soft Mode B
    // discs) resolve to cap-off + fade 0 (legacy path).
    float hardDistPastRim = kFogRimFadeCells;

    // Live analytic vision circles: max-combine each disc's smooth visibility,
    // evaluated against the CONTINUOUS world column so the edge is crisp at
    // render resolution and reveals partial voxels as a moving observer slides
    // sub-cell. The rim is floored at one canvas pixel for zoom-stable AA;
    // count 0 leaves `state` as the grid value (legacy path).
    if (visionCircleCount > 0) {
        // Local world-units-per-pixel from the iso inverse-projection Jacobian:
        // recover the +x neighbour at the same depth and measure the world step.
        // Zoom- and iso-correct, so the AA rim stays ~1px at any zoom with no
        // uniform.
        const vec3 pos3DNeighborX = trixelCanvasPixelToWorld3D(
            pixel + ivec2(1, 0), rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset,
            voxelRenderOptions, rasterYaw
        );
        const float worldPerPixel = length(pos3DNeighborX.xy - pos3D.xy);
        // Shared with VOXEL_TO_TRIXEL_STAGE_1's per-voxel clip so the floor's
        // per-pixel reveal here and the voxel-object edge there trace the same
        // analytic curve (#2102). worldPerPixel floors the rim at ~1 canvas px.
        for (int i = 0; i < visionCircleCount; ++i) {
            const float reveal =
                fogVisionCircleReveal(pos3D.xy, visionCircles[i], worldPerPixel);
            state = max(state, reveal);
            if (visionCircles[i].w == 0.0) {
                hardDistPastRim = min(
                    hardDistPastRim,
                    length(pos3D.xy - visionCircles[i].xy) - visionCircles[i].z
                );
            }
        }
    }

    // Fully-visible fast path: pass through untouched. Skips the store.
    if (state >= 1.0) {
        return;
    }

    const vec4 src = imageLoad(trixelColors, pixel);

    // Cross-section cap (#2124). A fog-hidden pixel within the cap band shows
    // matter the vision cylinder slices; tint its VERTICAL faces as the cut
    // surface so a sliced object reads as capped rather than hollow. The test
    // is pure per-pixel geometry — the rasterized face's axis (the depth
    // encoding's slot bits resolved through visibleFaceIds, the same mapping
    // AO and lighting use) and the surface column's radial distance past the
    // rim (hardDistPastRim — the metric the reveal and the rim fade key on):
    //   * TOP (Z) faces are never capped: flat ground and object tops fade on
    //     the plain radial curve. The retired ray+occupancy variant capped any
    //     solid-backed surface — on flat ground its view ray entered the
    //     floor's own interior underground and the march found it solid,
    //     painting a bright ring hugging the rim on the ray-facing (down-
    //     screen) side only: view-dependent and asymmetric.
    //   * VERTICAL (X/Y) faces blend the cut tint over the fade with a
    //     weight that is 1 at the rim and reaches 0 exactly at
    //     kFogCutMaxRimCells, so the wall is ONE continuous curve that
    //     CONVERGES to the plain fade — the top face's exact tone — at the
    //     band's end. (A binary in-band cap stepped from the cut tint
    //     straight to the fade at the band edge: a harsh line down the wall,
    //     dithered into voxel-stepped teeth by the per-voxel-quantized depth
    //     recovery, while the capless top face faded smoothly past it. The
    //     earlier ray+occupancy march had worse content-dependent bands.)
    //     The geometric test has no content dependence — no bands, no teeth,
    //     and no light-occlusion-bitfield read (no external wiring).
    // Colour-only, after lighting: AO, the sun bake, and lighting see no new
    // geometry. Hard discs only — a soft Mode B disc's wide falloff IS its
    // fade, and hardDistPastRim keeps its no-hard-disc init there, zeroing
    // the cap blend. `state` carries the disc's ~1px AA rim, so the junction
    // with visible matter stays antialiased.

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
    if (gridState < kFogExploredValue) {
        // Rim fade (see the const block): lift UNEXPLORED pixels toward their
        // lit colour near the hard-disc rim. Explored memory keeps its tone.
        // The squared ease-out crushes the fade tail to black well before the
        // keep-ring drop, so the outermost kept columns' wall faces (whose
        // constant-depth recovery reads a column slightly INSIDE their true
        // one) can't catch a visible lift against the void behind them.
        const float u = 1.0 - smoothstep(0.0, kFogRimFadeCells, hardDistPastRim);
        outColor = mix(outColor, src.rgb, kFogRimFadeLevel * u * u);
        // Feathered cross-section cap on vertical faces (see the block
        // comment above the fade).
        const int faceAxis = visibleFaceIds[encoded & 3] >> 1;
        if (visionCircleCount > 0 && faceAxis != 2) {
            const float capBlend =
                1.0 - smoothstep(0.0, kFogCutMaxRimCells, max(hardDistPastRim, 0.0));
            const vec3 capColor =
                mix(src.rgb * kFogCutTone + vec3(kFogCutLift), src.rgb, state);
            outColor = mix(outColor, capColor, capBlend);
        }
    }
    imageStore(trixelColors, pixel, vec4(outColor, src.a));
}
