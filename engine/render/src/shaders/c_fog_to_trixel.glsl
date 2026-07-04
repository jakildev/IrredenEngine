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
    // 1 when the camera-anchored light-occlusion bitfield is live at binding
    // 28 (BUILD_LIGHT_OCCLUSION_GRID registered this frame) — enables the
    // analytic cross-section band below. 0 = fall through to the plain dark
    // mask. Stamped by SYSTEM FOG_TO_TRIXEL on upload; rides the struct's
    // first padding lane, so the CPU layout is unchanged.
    int fogCutSolidsAvailable;
};

// World-occupancy source for the analytic cross-section band: the
// camera-anchored light-occlusion voxel bitfield built each frame by
// BUILD_LIGHT_OCCLUSION_GRID (full pool, world-space — invariant 1 in
// engine/render/CLAUDE.md). Header + indexing mirror
// c_propagate_light_volume.glsl::voxelOcclusionGetBit — keep in sync.
// Binding 28 is the shared alias slot (sun-shadow depth map / per-axis
// resolve scratch also ride it), so SYSTEM FOG_TO_TRIXEL re-binds the grid
// buffer for this dispatch; when the grid system never registered,
// fogCutSolidsAvailable is 0 and this buffer is never read.
const int kLightOcclusionGridSize = 256;
const int kLightOcclusionGridHalfExtent = 128;
layout(std430, binding = 28) readonly buffer LightOcclusionGrid {
    ivec4 occlusionWorldOrigin;
    uint occlusionBits[];
};

bool fogCutVoxelSolid(ivec3 w) {
    const int he = kLightOcclusionGridHalfExtent;
    const int lx = w.x - occlusionWorldOrigin.x;
    const int ly = w.y - occlusionWorldOrigin.y;
    const int lz = w.z - occlusionWorldOrigin.z;
    if (lx < -he || lx >= he || ly < -he || ly >= he || lz < -he || lz >= he) {
        return false;
    }
    const uint flatIndex =
        (uint(lz + he) * uint(kLightOcclusionGridSize) + uint(ly + he)) *
            uint(kLightOcclusionGridSize) +
        uint(lx + he);
    return ((occlusionBits[flatIndex >> 5u] >> (flatIndex & 31u)) & 1u) == 1u;
}

// Analytic cross-section band tuning. The ray probe is the finite-difference
// span (raw-depth units) used to recover this pixel's view-ray direction from
// the same inverse projection the mask uses — wide enough to dodge fp
// cancellation, tiny against the 16-bit depth range. The tone is the factor
// applied to the hidden surface's lit colour where the cut repaints it, so
// the cross-section reads as the object's inside rather than its surface;
// the lift is a small constant floor added on top so a cut through DARK
// matter (a near-black floor) still reads against the fog black instead of
// vanishing — hue-preserving for bright materials, a subtle gray-up for
// dark ones.
const int kFogCutRayProbeDepth = 8;
const float kFogCutTone = 0.85;
const float kFogCutLift = 0.06;

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
    // Kept separate from the circle-combined `state`: the cross-section band
    // below applies only to UNEXPLORED surfaces (explored memory keeps its
    // desaturated tone).
    const float gridState = fogTap(fogCell, fogSize);
    float state = gridState;
    // Hard-disc-only reveal, tracked separately so the cross-section band's
    // rim shortcut can't fire off a soft (Mode B) disc's wide falloff.
    float hardState = 0.0;

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
                hardState = max(hardState, reveal);
            }
        }
    }

    // Fully-visible fast path: pass through untouched. Skips the store.
    if (state >= 1.0) {
        return;
    }

    const vec4 src = imageLoad(trixelColors, pixel);

    // Analytic cross-section band (#2124). A pixel that would fog-dark shows
    // fog-hidden matter; removing that matter exposes the vision cylinder's
    // cut surface wherever the pixel's view ray crosses a disc INSIDE solid
    // voxels. Recover the ray from the SAME inverse projection the mask uses
    // (finite difference over depth — exact and yaw-correct), intersect it
    // with each hard disc analytically, and ask the light-occlusion bitfield
    // whether the entry point is solid. Solid ⇒ repaint this pixel as the
    // toned lit colour of the hidden surface — the cut. Runs after lighting
    // and writes COLOUR ONLY: the cut never touches trixelDistances, so AO,
    // the sun-shadow bake, and lighting are structurally unaffected (a
    // raster-injected band creased AO and cast phantom shadows around the
    // junction), and the silhouette is per-pixel analytic — no lattice steps.
    // Hard discs only (a soft-edged Mode B disc keeps its faded look); air at
    // the entry (thin hidden wall in front of the disc) falls through to the
    // plain dark mask.
    if (fogCutSolidsAvailable != 0 && gridState < kFogExploredValue &&
        visionCircleCount > 0) {
        bool cutSolid = false;
        if (hardState > 0.0) {
            // Rim pixel (inside the disc's AA band): the rasterized surface
            // itself straddles the cut, so it IS the solid matter being cut —
            // no ray test. Deciding these pixels by the ray instead dashes
            // both the arc junction and the cylinder-tangent locus (t ≈ 0 and
            // the discriminant degenerate exactly there).
            cutSolid = true;
        } else {
            // World step per raw-depth unit along this pixel's view ray.
            const vec3 posDeeper = trixelCanvasPixelToWorld3D(
                pixel, rawDepth + kFogCutRayProbeDepth, trixelCanvasOffsetZ1,
                frameCanvasOffset, voxelRenderOptions, rasterYaw
            );
            const vec3 rayStep = (posDeeper - pos3D) / float(kFogCutRayProbeDepth);
            const float a = dot(rayStep.xy, rayStep.xy);
            float bestT = -1.0;
            if (a > 0.0) {
                for (int i = 0; i < visionCircleCount; ++i) {
                    if (visionCircles[i].w != 0.0) {
                        continue; // hard discs only (Mode A)
                    }
                    const vec2 d0 = pos3D.xy - visionCircles[i].xy;
                    const float b = 2.0 * dot(d0, rayStep.xy);
                    const float c =
                        dot(d0, d0) - visionCircles[i].z * visionCircles[i].z;
                    const float det = b * b - 4.0 * a * c;
                    if (det <= 0.0) {
                        continue; // ray misses this disc
                    }
                    // First crossing at-or-behind the hidden surface. A
                    // negative entry root means the ray is already past this
                    // disc (a far-side surface) — no cut there by construction.
                    const float t = (-b - sqrt(det)) / (2.0 * a);
                    if (t >= 0.0 && (bestT < 0.0 || t < bestT)) {
                        bestT = t;
                    }
                }
            }
            if (bestT >= 0.0) {
                const float worldPerDepthUnit = length(rayStep);
                if (bestT * worldPerDepthUnit <= 1.0) {
                    // Entry within one cell of the hit surface — still inside
                    // the surface voxel's own matter: solid by construction,
                    // immune to bitfield rounding right at the rim.
                    cutSolid = true;
                } else {
                    // Sample occupancy half a world cell INSIDE the rim so the
                    // rounded cell is unambiguously interior; a second sample
                    // one cell deeper rides over rim-cell quantization misses.
                    const float tHalfCell = 0.5 / worldPerDepthUnit;
                    const ivec3 cellA =
                        roundHalfUp(pos3D + (bestT + tHalfCell) * rayStep);
                    const ivec3 cellB =
                        roundHalfUp(pos3D + (bestT + 3.0 * tHalfCell) * rayStep);
                    cutSolid = fogCutVoxelSolid(cellA) || fogCutVoxelSolid(cellB);
                }
            }
        }
        if (cutSolid) {
            // `state` carries the disc's ~1px AA rim, so the junction with
            // visible matter stays antialiased.
            const vec3 cutColor = src.rgb * kFogCutTone + vec3(kFogCutLift);
            imageStore(trixelColors, pixel, vec4(mix(cutColor, src.rgb, state), src.a));
            return;
        }
    }
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
