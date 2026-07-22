// Shared voxel face-selection + per-axis store-key math for the
// c_voxel_to_trixel stage-1 / stage-2 kernel family. Both stage BODIES used to
// carry byte-identical copies of everything here under "MUST mirror stage 1
// EXACTLY" comments — a one-sided edit silently desynced the colour tap from
// the distance tap. One definition makes that class of bug unrepresentable.
//
// This is an include-FRAGMENT (like the stage bodies): the kernel wrappers
// include it AFTER ir_iso_common.glsl / ir_constants.glsl and BEFORE the stage
// body, with the fog-grid image slot supplied as a compile-time macro:
//   #define IR_VOXEL_FOG_GRID_BINDING {0|3}
// STAGE_1 binds the fog grid on image slot 0 (free there — it writes only the
// distance image on slot 1); STAGE_2's slot 0 is the colour output, so it
// binds the grid on slot 3. A wrapper-#define'd binding is what makes the
// single definition possible — image slots cannot be runtime-parameterized in
// GLSL/MSL, which is why the functions were historically duplicated per stage.
// Metal twin: metal/ir_voxel_face_select.metal — keep byte-identical math.

// Per-voxel analytic fog clip inputs (#2102), mirroring
// c_voxel_visibility_compact + c_fog_to_trixel. The world fog canvas binds its
// 256² grid texture and uploads the live vision circles at binding 27; every
// non-fog / detached canvas binds the shared 1×1 all-visible placeholder + a
// count-0 observer buffer, so `fogColumnReveal` short-circuits to "fully
// visible" and those scenes stay byte-identical.
const int kFogOfWarHalfExtent = 128;
const float kFogExploredThreshold = 0.25;
const int kMaxFogVisionCircles = 8; // mirror of component_canvas_fog_of_war.hpp kMaxFogVisionCircles
layout(rgba8, binding = IR_VOXEL_FOG_GRID_BINDING) readonly uniform image2D canvasFogOfWar;
layout(std140, binding = 27) uniform FogObserverData {
    vec4 visionCircles[kMaxFogVisionCircles]; // (centerX, centerY, radius, edgeSoftness)
    int visionCircleCount;
};

// Fog reveal of world grid COLUMN `col` in [0,1]. Stage 1 emits the cut face's
// DISTANCE for `reveal < 1.0` (#2126 P2) and stage 2 paints colour on the same
// set of faces — both through this one definition, so the cut wall's depth and
// colour cannot desync. Explored grid memory and in/at-disc columns are kept;
// the 1×1 placeholder + OOB columns read as fully visible, matching the
// OOB-as-visible invariant — so non-fog / detached canvases are byte-identical.
float fogColumnReveal(ivec2 col) {
    const ivec2 fogSize = imageSize(canvasFogOfWar);
    if (fogSize.x <= 1) {
        return 1.0; // 1×1 all-visible placeholder (non-fog / detached canvas)
    }
    const ivec2 cell = col + ivec2(kFogOfWarHalfExtent);
    if (cell.x < 0 || cell.x >= fogSize.x || cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0; // out-of-range column reads as visible
    }
    if (imageLoad(canvasFogOfWar, cell).r >= kFogExploredThreshold) {
        return 1.0; // explored / visible grid memory — keep (FOG_TO_TRIXEL fades it)
    }
    float reveal = 0.0;
    for (int i = 0; i < visionCircleCount; ++i) {
        reveal = max(reveal, fogVisionCircleReveal(vec2(col), visionCircles[i], 0.0));
    }
    return reveal;
}

// Own-column DROP reveal, evaluated at the cell point NEAREST each vision-circle
// center rather than the cell center (#2124 screen-space cross-section). The
// drop keeps a column iff this is > 0, so a column the disc merely CLIPS
// (center outside R, unit cell still overlaps the reveal region) is KEPT and
// rasters its full footprint; FOG_TO_TRIXEL then trims it per pixel at the
// exact analytic edge instead of the geometry ending on the voxel lattice (the
// #2102 voxel-jagged edge). Evaluating at the nearest cell point keeps ONLY the
// one-cell ring the disc crosses; kFogColumnKeepAa is a small rim so the
// hard-disc smoothstep is non-degenerate.
//
// kFogHiddenKeepCells widens that keep into a RING of fog-hidden columns
// around the disc (#2124 analytic cross-section): FOG_TO_TRIXEL's image-space
// cut repaints a hidden pixel as the cylinder's cut surface, so hidden matter
// near the rim must still RENDER (lit, normally shaded) for the cut to have
// colour to work from. The ring bounds how deep a cut face can be recovered
// (≈ keep/√2 cells of height); columns past it drop as before. Ring voxels
// also resume casting sun shadows / AO near the rim. Mirrored in
// c_voxel_visibility_compact.{glsl,metal}'s kCullSafetyCells (keep superset).
const float kFogColumnCellHalf = 0.5;
const float kFogColumnKeepAa = 0.5;
const float kFogHiddenKeepCells = 8.0;
float fogColumnRevealNearest(ivec2 col) {
    const ivec2 fogSize = imageSize(canvasFogOfWar);
    if (fogSize.x <= 1) {
        return 1.0;
    }
    const ivec2 cell = col + ivec2(kFogOfWarHalfExtent);
    if (cell.x < 0 || cell.x >= fogSize.x || cell.y < 0 || cell.y >= fogSize.y) {
        return 1.0;
    }
    if (imageLoad(canvasFogOfWar, cell).r >= kFogExploredThreshold) {
        return 1.0;
    }
    float reveal = 0.0;
    for (int i = 0; i < visionCircleCount; ++i) {
        const vec2 nearest = clamp(
            visionCircles[i].xy, vec2(col) - kFogColumnCellHalf, vec2(col) + kFogColumnCellHalf
        );
        reveal = max(
            reveal,
            fogVisionCircleReveal(
                nearest, visionCircles[i], kFogColumnKeepAa + kFogHiddenKeepCells
            )
        );
    }
    return reveal;
}

// The face-selection verdict both stage kernels branch on. `keepFace == false`
// ⇒ the caller returns without emitting. `isCutFace` marks a non-exposed
// VERTICAL face kept ONLY by the fog cut rule — the object's interior
// cross-section wall; stage 2 folds it into the stored entity id (bit 29) so
// LIGHTING_TO_TRIXEL force-lights it (#2124), stage 1 ignores it.
struct VoxelFaceSelect {
    int faceId;                 // possibly riser-flipped (#2207)
    int riserFlip;              // 1 = opposite polarity of the slot's triplet face
    bool rotatedEmit;           // rotated-content gate (re-voxelize OR kRotatedEmit)
    bool bothPolaritiesExposed; // #2157 dual-emit predicate (cardinal subdivided path)
    bool fogActive;             // world fog route gate (#2125/#2127/#2128)
    ivec2 worldColumn;          // world fog column (valid iff fogActive)
    bool keepFace;
    bool isCutFace;
};

// Face selection for one (voxel, slot) invocation — the visible-triplet ×
// exposed-mask gate (#1278), the silhouette-riser flip (#2207) and dual-emit
// predicate (#2157) for rotated content, and the fog cut-face widening
// (#2125/#2126/#2127; per-axis #2128). Stage 1 keys its distance taps and
// stage 2 its colour/entity-id taps off the SAME verdict, which is the
// contract that keeps the two kernels' face sets identical.
//
// The `perAxisRouteIn <= 2` comparison term in `fogActive` is load-bearing for
// byte-identity: it makes the GLSL/MSL compiler schedule the non-fog per-axis
// store identically to the pre-#2128 `== 0` gate (a bare-removed term
// reshuffled the Metal per-axis tie-winner resolution). A world-placed
// re-voxelize detached canvas rasters in the pool-centered MODEL frame, so its
// world column is model + detachedWorldReceiveIn.xy — the same recovery
// c_lighting_to_trixel uses. ±Z faces are never cut (the vision region is a
// vertical cylinder), hence the `faceId < kFaceZNeg` bound.
VoxelFaceSelect selectVoxelFace(
    const int faceIdIn,
    const bool reVoxelize,
    const uint reserved,
    const uint flagsByte,
    const vec4 voxelPosition,
    const int perAxisRouteIn,
    const float isDetachedCanvasIn,
    const vec4 detachedWorldReceiveIn
) {
    VoxelFaceSelect sel;
    sel.faceId = faceIdIn;
    // Silhouette-riser face selection (rotated-footprint gap fix): if this
    // slot's triplet face is occluded but the opposite same-axis face is
    // exposed, emit that opposite face — the missing silhouette riser on a
    // rotated staircase edge. Gated to ROTATED content (the re-voxelize
    // uniform OR the per-voxel kRotatedEmit marker, reserved bit 2), so
    // axis-aligned fast paths never flip and stay byte-identical.
    sel.rotatedEmit = reVoxelize || (reserved & 4u) != 0u;
    sel.riserFlip = 0;
    if (sel.rotatedEmit && !faceIsExposed(flagsByte, sel.faceId) &&
        faceIsExposed(flagsByte, sel.faceId ^ 1)) {
        sel.faceId = sel.faceId ^ 1;
        sel.riserFlip = 1;
    }
    // Both-exposed silhouette-riser dual emit (#2157): a rotated staircase
    // EDGE cell can have BOTH polarities of a slot's axis exposed; the flip
    // above never fires there, so the cardinal subdivided path emits both
    // planes (the per-axis store takes NO second tap — measured net-worse,
    // see #2207).
    sel.bothPolaritiesExposed = sel.rotatedEmit && faceIsExposed(flagsByte, sel.faceId) &&
        faceIsExposed(flagsByte, sel.faceId ^ 1);
    // World fog route: fires on the world fog route (perAxisRoute==0 — the
    // GRID world canvas or a world-placed re-voxelize detached canvas, #2127)
    // AND the X/Y per-axis rotation routes (1/2, #2128 — a cut face is a
    // vertical X/Y face that rides the matching axis canvas under continuous
    // yaw). Plain octahedral DETACHED and the Z route (3, no cut faces) stay
    // on the no-fog placeholder.
    sel.fogActive = visionCircleCount > 0 && perAxisRouteIn <= 2 &&
        (isDetachedCanvasIn < 0.5 ||
         (perAxisRouteIn == 0 && detachedWorldReceiveIn.w != 0.0));
    sel.worldColumn = ivec2(0);
    if (sel.fogActive) {
        sel.worldColumn = roundHalfUp(voxelPosition.xyz).xy +
            (isDetachedCanvasIn > 0.5 ? roundHalfUp(detachedWorldReceiveIn.xy) : ivec2(0));
    }
    // Exposed-face gate (#1278) widened with the fog CUT-FACE rule: at the fog
    // boundary a non-exposed VERTICAL face becomes the interior cross-section
    // wall when the solid neighbor COLUMN it faces is not fully revealed
    // (`reveal < 1.0`, #2126 P2 — the cut exists across the whole soft band and
    // the per-pixel FOG_TO_TRIXEL mask owns the smooth silhouette; a hard disc
    // collapses to the binary boundary, byte-identical).
    sel.keepFace = faceIsExposed(flagsByte, sel.faceId);
    sel.isCutFace = false;
    if (!sel.keepFace && sel.faceId < kFaceZNeg && sel.fogActive) {
        sel.keepFace =
            fogColumnReveal(sel.worldColumn + faceOutwardNormal6I(sel.faceId).xy) < 1.0;
        sel.isCutFace = sel.keepFace;
    }
    return sel;
}

// Per-axis base-resolution store position + encoded key (#1458 encoding,
// #1944 un-yawed cardinal iso key). Returns the face-plane position whose
// projection `perAxisBase + pos3DtoPos2DIso(facePos)` is the store cell;
// writes the encoded distance key through `encodedDistance`. Stage 1's
// distance/mask/append/election taps and stage 2's colour tap all derive the
// cell + key through this one definition — the "stage 2 MUST mirror stage 1's
// store exactly" contract in code. Full sub-cell fracs (u/v in-plane + w
// out-of-plane) ride the encoding so a fractionally-positioned face
// reconstructs on its TRUE plane; integer content encodes 8/8/8 (zero
// offsets). The 4-bit frac quantization here is where equal keys arise — two
// sub-cell offsets in one 1/16 bucket encode byte-identically — which is why
// the #2255 winner election exists downstream.
ivec3 perAxisStoreFacePos(
    const vec4 voxelPosition,
    const int faceId,
    const int slot,
    const int axis,
    const int riserFlip,
    out int encodedDistance
) {
    const vec3 worldAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const ivec3 worldPos = roundHalfUp(worldAligned);
    const ivec3 facePos = faceMicroPositionFixed6(faceId, worldPos, 0, 0, 1);
    const vec3 fracInCell = worldAligned - vec3(worldPos);
    encodedDistance =
        encodeDepthWithFaceFrac(pos3DtoDistance(facePos), slot, axis, fracInCell, riserFlip);
    return facePos;
}
