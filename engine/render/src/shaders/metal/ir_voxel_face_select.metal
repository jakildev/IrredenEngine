// Shared voxel face-selection + per-axis store-key math for the
// c_voxel_to_trixel stage-1 / stage-2 kernel family — Metal twin of
// ../ir_voxel_face_select.glsl (keep byte-identical math). Both stage BODIES
// used to carry byte-identical copies of everything here under "MUST mirror
// stage 1 EXACTLY" comments; one definition makes a one-sided edit
// unrepresentable. Included by the kernel wrappers AFTER ir_iso_common.metal /
// ir_constants.metal and BEFORE the stage body. Metal passes the fog texture +
// observer buffer as function arguments (no global bindings), so no
// fog-binding macro is needed here — the GLSL side #defines
// IR_VOXEL_FOG_GRID_BINDING instead.
//
// No `#ifndef ..._INCLUDED` self-guard (unlike the ir_per_axis_lighting.metal
// precedent) is needed: every function here is `static` (internal linkage) and
// each wrapper includes this file exactly once, so neither in-TU re-inclusion
// nor a standalone glob-compile can raise a duplicate-symbol conflict.
// ir_per_axis_lighting.metal carries its guard only because it uses
// external-linkage `inline` functions — do NOT "fix" this file by switching
// its functions to `inline` and reintroducing that hazard.

// Prerequisite helpers (faceIsExposed, roundHalfUp, fogVisionCircleReveal,
// faceMicroPositionFixed6, encodeDepthWithFaceFrac, …). The runtime include
// resolver is recursive with a visited set, so the wrapper's own earlier
// include of ir_iso_common.metal makes this a suppressed duplicate — the
// ir_per_axis_lighting.metal idiom.
#include "ir_iso_common.metal"

// Per-voxel analytic fog clip inputs (#2102), mirroring
// c_voxel_visibility_compact + c_fog_to_trixel. The world fog canvas binds its
// 256² grid + live vision circles; every non-fog / detached canvas binds a 1×1
// all-visible placeholder + count-0 observers, so `fogColumnReveal`
// short-circuits to "fully visible" and those scenes stay byte-identical.
constant int kFogOfWarHalfExtent = 128;
constant float kFogExploredThreshold = 0.25f;
constant int kMaxFogVisionCircles = 8; // mirror of component_canvas_fog_of_war.hpp kMaxFogVisionCircles
struct FogObserverData {
    float4 visionCircles[kMaxFogVisionCircles]; // (centerX, centerY, radius, edgeSoftness)
    int visionCircleCount;
    int _fogObsPad0;
    int _fogObsPad1;
    int _fogObsPad2;
    // Per-circle height penalty (#2260), appended after the ivec4 tail to match
    // FrameDataFogObservers::visionCircleHeights_ (offset 144) and the GLSL
    // block. heights[i] = (observerZ, zCost, 0, 0), read only by stage 1's
    // own-column DROP (fogColumnRevealZ / fogColumnRevealNearestZ in
    // c_voxel_to_trixel_stage_1_body.metal); the selection math below ignores
    // it. zCost 0 (the default) → the drop is byte-identical to the pre-#2260
    // 2D clip.
    float4 visionCircleHeights[kMaxFogVisionCircles];
};

// Fog reveal of world grid COLUMN `col` in [0,1]. Stage 1 emits the cut face's
// DISTANCE for `reveal < 1.0` (#2126 P2) and stage 2 paints colour on the same
// set of faces — both through this one definition, so the cut wall's depth and
// colour cannot desync. GLSL twin: fogColumnReveal in
// ../ir_voxel_face_select.glsl.
static float fogColumnReveal(
    texture2d<float, access::read> fog, constant FogObserverData& obs, int2 col
) {
    const int2 fogSize = int2(int(fog.get_width()), int(fog.get_height()));
    if (fogSize.x <= 1) {
        return 1.0f; // 1×1 all-visible placeholder (non-fog / detached canvas)
    }
    const int2 cell = col + int2(kFogOfWarHalfExtent);
    if (cell.x < 0 || cell.x >= fogSize.x || cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0f; // out-of-range column reads as visible
    }
    if (fog.read(uint2(cell)).r >= kFogExploredThreshold) {
        return 1.0f; // explored / visible grid memory — keep
    }
    float reveal = 0.0f;
    for (int i = 0; i < obs.visionCircleCount; ++i) {
        reveal = max(reveal, fogVisionCircleReveal(float2(col), obs.visionCircles[i], 0.0f));
    }
    return reveal;
}

// Own-column DROP reveal, evaluated at the cell point NEAREST each
// vision-circle center (#2124 screen-space cross-section). The drop keeps a
// column iff this is > 0; kFogHiddenKeepCells widens the keep into a ring of
// fog-hidden columns so FOG_TO_TRIXEL's image-space cut has hidden matter to
// repaint. Grid-memory / OOB / placeholder short-circuits match
// fogColumnReveal. GLSL twin in ../ir_voxel_face_select.glsl.
constant float kFogColumnCellHalf = 0.5f;
constant float kFogColumnKeepAa = 0.5f;
constant float kFogHiddenKeepCells = 8.0f;
static float fogColumnRevealNearest(
    texture2d<float, access::read> fog, constant FogObserverData& obs, int2 col
) {
    const int2 fogSize = int2(int(fog.get_width()), int(fog.get_height()));
    if (fogSize.x <= 1) {
        return 1.0f;
    }
    const int2 cell = col + int2(kFogOfWarHalfExtent);
    if (cell.x < 0 || cell.x >= fogSize.x || cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0f;
    }
    if (fog.read(uint2(cell)).r >= kFogExploredThreshold) {
        return 1.0f;
    }
    float reveal = 0.0f;
    for (int i = 0; i < obs.visionCircleCount; ++i) {
        const float2 nearest = clamp(
            obs.visionCircles[i].xy,
            float2(col) - kFogColumnCellHalf,
            float2(col) + kFogColumnCellHalf
        );
        reveal = max(
            reveal,
            fogVisionCircleReveal(
                nearest, obs.visionCircles[i], kFogColumnKeepAa + kFogHiddenKeepCells
            )
        );
    }
    return reveal;
}

// The face-selection verdict both stage kernels branch on. `keepFace == false`
// ⇒ the caller returns without emitting. `isCutFace` marks a non-exposed
// VERTICAL face kept ONLY by the fog cut rule — stage 2 folds it into the
// stored entity id (bit 29, #2124), stage 1 ignores it.
struct VoxelFaceSelect {
    int faceId;                 // possibly riser-flipped (#2207)
    int riserFlip;              // 1 = opposite polarity of the slot's triplet face
    bool bothPolaritiesExposed; // #2157 dual-emit predicate (cardinal subdivided path)
    bool fogActive;             // world fog route gate (#2125/#2127/#2128)
    int2 worldColumn;           // world fog column (valid iff fogActive)
    bool keepFace;
    bool isCutFace;
};

// Face selection for one (voxel, slot) invocation — the visible-triplet ×
// exposed-mask gate (#1278), the silhouette-riser flip (#2207) and dual-emit
// predicate (#2157) for rotated content, and the fog cut-face widening
// (#2125/#2126/#2127; per-axis #2128). Stage 1 keys its distance taps and
// stage 2 its colour/entity-id taps off the SAME verdict. See the GLSL twin
// for the full per-rule rationale (incl. why the `perAxisRouteIn <= 2`
// comparison term is load-bearing for non-fog byte-identity).
static VoxelFaceSelect selectVoxelFace(
    texture2d<float, access::read> fog,
    constant FogObserverData& obs,
    const int faceIdIn,
    const bool reVoxelize,
    const uint reserved,
    const uint flagsByte,
    const float4 voxelPosition,
    const int perAxisRouteIn,
    const float isDetachedCanvasIn,
    const float4 detachedWorldReceiveIn
) {
    VoxelFaceSelect sel;
    sel.faceId = faceIdIn;
    // Function-local intermediate — only the riserFlip gate and
    // bothPolaritiesExposed predicate read it, so it stays off the verdict struct.
    const bool rotatedEmit = reVoxelize || (reserved & 4u) != 0u;
    sel.riserFlip = 0;
    if (rotatedEmit && !faceIsExposed(flagsByte, sel.faceId) &&
        faceIsExposed(flagsByte, sel.faceId ^ 1)) {
        sel.faceId = sel.faceId ^ 1;
        sel.riserFlip = 1;
    }
    sel.bothPolaritiesExposed = rotatedEmit && faceIsExposed(flagsByte, sel.faceId) &&
        faceIsExposed(flagsByte, sel.faceId ^ 1);
    sel.fogActive = obs.visionCircleCount > 0 && perAxisRouteIn <= 2 &&
        (isDetachedCanvasIn < 0.5f ||
         (perAxisRouteIn == 0 && detachedWorldReceiveIn.w != 0.0f));
    sel.worldColumn = int2(0);
    if (sel.fogActive) {
        sel.worldColumn = roundHalfUp(voxelPosition.xyz).xy +
            (isDetachedCanvasIn > 0.5f
                 ? roundHalfUp(detachedWorldReceiveIn.xy)
                 : int2(0));
    }
    sel.keepFace = faceIsExposed(flagsByte, sel.faceId);
    sel.isCutFace = false;
    if (!sel.keepFace && sel.faceId < kFaceZNeg && sel.fogActive) {
        sel.keepFace = fogColumnReveal(
            fog, obs, sel.worldColumn + faceOutwardNormal6I(sel.faceId).xy) < 1.0f;
        sel.isCutFace = sel.keepFace;
    }
    return sel;
}

// Per-axis base-resolution store position + encoded key (#1458 encoding,
// #1944 un-yawed cardinal iso key). Returns the face-plane position whose
// projection `perAxisBase + pos3DtoPos2DIso(facePos)` is the store cell;
// writes the encoded distance key through `encodedDistance`. The "stage 2
// MUST mirror stage 1's store exactly" contract in code — see the GLSL twin
// for the full rationale (frac carrier, #2255 equal-key source).
static int3 perAxisStoreFacePos(
    const float4 voxelPosition,
    const int faceId,
    const int slot,
    const int axis,
    const int riserFlip,
    thread int& encodedDistance
) {
    const float3 worldAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const int3 worldPos = roundHalfUp(worldAligned);
    const int3 facePos = faceMicroPositionFixed6(faceId, worldPos, 0, 0, 1);
    const float3 fracInCell = worldAligned - float3(worldPos);
    encodedDistance =
        encodeDepthWithFaceFrac(pos3DtoDistance(facePos), slot, axis, fracInCell, riserFlip);
    return facePos;
}
