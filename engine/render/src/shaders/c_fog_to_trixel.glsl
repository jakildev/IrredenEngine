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
};

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
    float state = fogTap(fogCell, fogSize);

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
        for (int i = 0; i < visionCircleCount; ++i) {
            const vec4 circle = visionCircles[i];
            const float dist = length(pos3D.xy - circle.xy);
            const float aa = max(circle.w, worldPerPixel);
            const float vis = 1.0 - smoothstep(circle.z - aa, circle.z + aa, dist);
            state = max(state, vis);
        }
    }

    // Fully-visible fast path: pass through untouched. Skips the store.
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
