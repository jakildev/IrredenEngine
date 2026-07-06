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
    // 1 when the camera-anchored light-occlusion bitfield is live at
    // buffer(28) (BUILD_LIGHT_OCCLUSION_GRID registered this frame) — enables
    // the analytic cross-section band. Stamped by SYSTEM FOG_TO_TRIXEL on
    // upload; rides the struct's first padding lane. See the GLSL twin.
    int fogCutSolidsAvailable;
};

// World-occupancy source for the analytic cross-section band — the
// camera-anchored light-occlusion voxel bitfield. Header + indexing mirror
// metal/c_propagate_light_volume.metal::voxelOcclusionGetBit; keep in sync.
// buffer(28) is the shared alias slot, re-bound by SYSTEM FOG_TO_TRIXEL for
// this dispatch (a placeholder buffer is bound when the grid system never
// registered, and fogCutSolidsAvailable gates every read).
constant int kLightOcclusionGridSize = 256;
constant int kLightOcclusionGridHalfExtent = 128;
struct FogLightOcclusionData {
    int4 worldOriginVoxel;
    uint bits[1];
};

static bool fogCutVoxelSolid(device const FogLightOcclusionData *occlusion, int3 w) {
    const int he = kLightOcclusionGridHalfExtent;
    const int lx = w.x - occlusion->worldOriginVoxel.x;
    const int ly = w.y - occlusion->worldOriginVoxel.y;
    const int lz = w.z - occlusion->worldOriginVoxel.z;
    if (lx < -he || lx >= he || ly < -he || ly >= he || lz < -he || lz >= he) {
        return false;
    }
    const uint flatIndex =
        (uint(lz + he) * uint(kLightOcclusionGridSize) + uint(ly + he)) *
            uint(kLightOcclusionGridSize) +
        uint(lx + he);
    return ((occlusion->bits[flatIndex >> 5u] >> (flatIndex & 31u)) & 1u) == 1u;
}

// Analytic cross-section band tuning — mirrors the GLSL twin (see there for
// the rationale, including the air-pocket march).
constant int kFogCutRayProbeDepth = 8;
constant float kFogCutTone = 0.85f;
constant float kFogCutLift = 0.06f;
constant int kFogCutMarchSteps = 8;
constant float kFogCutMaxEntryCells = 2.0f;
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
    device const FogLightOcclusionData *occlusionGrid [[buffer(28)]],
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
    // Hard-disc-only reveal, tracked separately so the cross-section band's
    // rim shortcut can't fire off a soft (Mode B) disc's wide falloff.
    float hardState = 0.0f;
    // This column's world distance PAST the nearest hard disc's radius —
    // drives the rim fade below. Initialized to the full fade width so
    // no-hard-disc scenes resolve to fade 0 (legacy path).
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
                hardState = max(hardState, reveal);
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

    // Analytic cross-section band (#2124) — mirrors the GLSL twin exactly
    // (see there for the full rationale): intersect this pixel's view ray
    // with each hard disc, ask the occlusion bitfield whether the entry
    // point is solid, and repaint solid entries as the toned lit colour of
    // the hidden surface. Colour-only, after lighting — never touches
    // trixelDistances, so AO / sun-shadow / lighting see no new geometry.
    if (fogObservers.fogCutSolidsAvailable != 0 && gridState < kFogExploredValue &&
        fogObservers.visionCircleCount > 0) {
        bool cutSolid = false;
        if (hardState > 0.0f) {
            // Rim pixel (inside the disc's AA band): the rasterized surface
            // itself straddles the cut, so it IS the solid matter being cut —
            // no ray test. Deciding these pixels by the ray instead dashes
            // both the arc junction and the cylinder-tangent locus (t ≈ 0 and
            // the discriminant degenerate exactly there).
            cutSolid = true;
        } else {
            const float3 posDeeper = trixelCanvasPixelToWorld3D(
                pixel,
                rawDepth + kFogCutRayProbeDepth,
                frameData.trixelCanvasOffsetZ1,
                frameData.frameCanvasOffset,
                frameData.voxelRenderOptions,
                frameData.rasterYaw
            );
            const float3 rayStep = (posDeeper - pos3D) / float(kFogCutRayProbeDepth);
            const float a = dot(rayStep.xy, rayStep.xy);
            float bestT = -1.0f;
            if (a > 0.0f) {
                for (int i = 0; i < fogObservers.visionCircleCount; ++i) {
                    if (fogObservers.visionCircles[i].w != 0.0f) {
                        continue; // hard discs only (Mode A)
                    }
                    const float2 d0 = pos3D.xy - fogObservers.visionCircles[i].xy;
                    const float b = 2.0f * dot(d0, rayStep.xy);
                    const float c = dot(d0, d0) -
                        fogObservers.visionCircles[i].z * fogObservers.visionCircles[i].z;
                    const float det = b * b - 4.0f * a * c;
                    if (det <= 0.0f) {
                        continue; // ray misses this disc
                    }
                    // First crossing at-or-behind the hidden surface; a
                    // negative entry root means the ray is already past this
                    // disc (a far-side surface) — no cut there by construction.
                    const float t = (-b - sqrt(det)) / (2.0f * a);
                    if (t >= 0.0f && (bestT < 0.0f || t < bestT)) {
                        bestT = t;
                    }
                }
            }
            if (bestT >= 0.0f) {
                const float worldPerDepthUnit = length(rayStep);
                const float entryDistance = bestT * worldPerDepthUnit;
                if (entryDistance <= 1.0f) {
                    // Entry within one cell of the hit surface — still inside
                    // the surface voxel's own matter: solid by construction,
                    // immune to bitfield rounding right at the rim.
                    cutSolid = true;
                } else if (entryDistance <= kFogCutMaxEntryCells) {
                    // March half-cell occupancy samples along the ray from
                    // just inside the rim (the first sample's half-cell inset
                    // keeps the rounded cell unambiguously interior). The
                    // march rides over rim-cell quantization misses AND small
                    // air pockets at an object's cut corner (solid floor a
                    // couple of cells behind the entry); a void entry finds
                    // only descending air and stays on the dark mask.
                    const float tHalfCell = 0.5f / worldPerDepthUnit;
                    for (int s = 1; s <= kFogCutMarchSteps && !cutSolid; ++s) {
                        cutSolid = fogCutVoxelSolid(
                            occlusionGrid,
                            roundHalfUp(pos3D + (bestT + float(s) * tHalfCell) * rayStep)
                        );
                    }
                }
            }
        }
        if (cutSolid) {
            // `state` carries the disc's ~1px AA rim, so the junction with
            // visible matter stays antialiased.
            const float3 cutColor = src.rgb * kFogCutTone + float3(kFogCutLift);
            trixelColors.write(float4(mix(cutColor, src.rgb, state), src.a), uint2(pixel));
            return;
        }
    }
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
    // Rim fade (see the const block): lift UNEXPLORED pixels toward their lit
    // colour near the hard-disc rim. Explored memory keeps its tone. The
    // squared ease-out crushes the fade tail to black well before the
    // keep-ring drop, so the outermost kept columns' wall faces (whose
    // constant-depth recovery reads a column slightly INSIDE their true one)
    // can't catch a visible lift against the void behind them.
    if (gridState < kFogExploredValue) {
        const float u = 1.0f - smoothstep(0.0f, kFogRimFadeCells, hardDistPastRim);
        outColor = mix(outColor, src.rgb, kFogRimFadeLevel * u * u);
    }
    trixelColors.write(float4(outColor, src.a), uint2(pixel));
}
