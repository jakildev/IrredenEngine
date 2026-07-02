/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_1.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#version 450 core

layout(local_size_x = 2, local_size_y = 3, local_size_z = 1) in;

#include "ir_iso_common.glsl"

// Coordinate chain: World 3D -> Iso 2D -> Canvas pixel
//   canvasPixel = trixelCanvasOffsetZ1 + floor(cameraIso) + pos3DtoPos2DIso(world)
// frameCanvasOffset holds floor(cameraIso) from the CPU side.
// Canvas Y increases upward; the trixel-to-framebuffer pass flips V.
layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;         // floor(cameraIso)
    uniform ivec2 trixelCanvasOffsetZ1;     // canvas origin for Z-face voxels
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single-canvas raster
    // (byte-identical); 1/2/3 = the X/Y/Z per-axis canvas pass (#1309).
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;         // trixel canvas dimensions
    uniform ivec2 cullIsoMin;               // iso-space cull viewport (matches CPU chunk mask)
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;                // continuous Z-yaw (radians)
    uniform float rasterYaw;                // cardinal-snap multiple of pi/2 nearest visualYaw
    // visualYaw - rasterYaw, in [-pi/4, pi/4]. The pre-T-293 screen-space
    // residual composite (T-058 / T-322) that consumed this value as a
    // post-trixel rotation was retired by T-323; faceDeform[] below now
    // absorbs it during the trixel emit instead.
    uniform float residualYaw;
    // 1.0 for a detached entity canvas, 0.0 for the world canvas. Gates
    // emitDeformedFace max super-sample level (world: 2, detached: 6).
    uniform float isDetachedCanvas;
    // Per-slot deformation matrix packed column-major into vec4: .xy = col0,
    // .zw = col1 of IRMath::faceDeformationMatrix(axis(visibleFaceIds[slot]),
    // residualYaw). **Indexed by visible-triplet SLOT (0/1/2)**, not by axis —
    // at non-zero cardinal the WORLD face whose matrix lives at slot s
    // changes per `visibleFaceIds[s]`. Identity at residualYaw==0 so the
    // cardinal-snap path stays bit-identical pixel-for-pixel against
    // rasterYaw-only master (T-293 + #1278).
    uniform vec4 faceDeform[3];
    // Per-slot world FaceId (0..5 = X_NEG/X_POS/Y_NEG/Y_POS/Z_NEG/Z_POS) —
    // the three camera-visible faces resolved by
    // `IRMath::visibleFaceTripletCardinal` on the CPU (#1278). Slot 0/1/2
    // map to the workgroup-local face slot returned by `localIDToFace_2x3`;
    // `.w` is std140 padding. At cardinal 0 the default {0, 2, 4} = {X_NEG,
    // Y_NEG, Z_NEG} matches the pre-#1278 lower-coordinate semantics.
    uniform ivec4 visibleFaceIds;
    // Model-frame iso depth axis `R⁻¹·(1,1,1)` for the per-voxel occlusion
    // metric (#1462). (1,1,1) for the world canvas / identity entity, so
    // isoDepthAlongAxis collapses to pos3DtoDistance and the GRID / identity
    // raster stays byte-identical; a rotated DETACHED canvas uploads
    // `IRMath::isoDepthAxisModel(rotation)`. `.w` is std140 padding. Appended
    // after visibleFaceIds (offset 144) — the prefix the other binding-7
    // shaders declare is unchanged, so only this stage reads it.
    uniform vec4 voxelDepthAxis;
    // World-receive offset for a world-placed detached re-voxelize solid
    // (#1576 P4b-2 / #2127). `.xyz` = the entity's world cell origin
    // (`roundVec3HalfUp(translation)`, the SAME offset c_lighting_to_trixel adds
    // to world-sample shadow + light); `.w` = 1.0 when world-placed (the default
    // since #1624), else 0.0. The detached re-voxelize canvas rasters its pool in
    // the pool-centered MODEL frame, so the fog cut-face + own-column tests below
    // recover each voxel's WORLD column as `round(voxelPosition.xy) + round(.xy)`
    // and decide "hidden" against the shared world fog grid. std140-appended after
    // voxelDepthAxis (offset 160) to match FrameDataVoxelToCanvas; the all-zero
    // default keeps the world / per-axis / screen-locked canvases unchanged.
    uniform vec4 detachedWorldReceive;
};

layout(std430, binding = 5) readonly buffer PositionBuffer {
    vec4 positions[];
};

// 12 B per voxel — must match C_Voxel layout in
// engine/prefabs/irreden/voxel/components/component_voxel.hpp.
struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};

// Stage 1 reads `materialFlagBone.flags` (bits 0..5 are face-occlusion bits
// — see VoxelFlags in component_voxel.hpp) to skip emitting iso-visible
// faces that are blocked by a neighbor. `voxels[]` is still bound for
// Phase 2 (#605), where stage 1 will multiply each voxel position by
// bone_matrix[bone_id] before projecting.
layout(std430, binding = 6) readonly buffer ColorBuffer {
    Voxel voxels[];
};

// Face-occlusion bit indices live at `2 + faceId` in `materialFlagBone`'s
// byte 5, mirroring `IRComponents::VoxelFlags::kFaceOccluded*` in
// engine/prefabs/irreden/voxel/components/component_voxel.hpp. The
// exposed-face test (visible-triplet × exposed-mask, #1278) is centralized
// in `faceIsExposed(flagsByte, faceId)` from ir_iso_common.glsl.

layout(std430, binding = 25) readonly buffer CompactedIndices {
    uint compactedVoxelIndices[];
};

layout(std430, binding = 26) readonly buffer IndirectDispatchParams {
    uint numGroupsX;
    uint numGroupsY;
    uint numGroupsZ;
    uint visibleCount;
};

layout(r32i, binding = 1) uniform iimage2D triangleCanvasDistances;

// Per-voxel analytic fog clip inputs (#2102), mirroring c_voxel_visibility_compact
// + c_fog_to_trixel. The world fog canvas binds its 256² grid texture on image
// slot 0 and uploads the live vision circles at binding 27; every non-fog /
// detached canvas binds the shared 1×1 all-visible placeholder + a count-0
// observer buffer, so `fogColumnReveal` short-circuits to "fully visible"
// and those scenes stay byte-identical. Slot 0 is free in STAGE_1 (it writes
// only the distance image on slot 1); STAGE_2 rebinds slot 0 to the colour
// image and reads the fog grid on slot 3 instead.
const int kFogOfWarHalfExtent = 128;
const float kFogExploredThreshold = 0.25;
const int kMaxFogVisionCircles = 8; // mirror of component_canvas_fog_of_war.hpp kMaxFogVisionCircles
layout(rgba8, binding = 0) readonly uniform image2D canvasFogOfWar;
layout(std140, binding = 27) uniform FogObserverData {
    vec4 visionCircles[kMaxFogVisionCircles]; // (centerX, centerY, radius, edgeSoftness)
    int visionCircleCount;
};

void writeDistanceTap(const ivec2 canvasPixel, const int voxelDistance) {
    if (!isInsideCanvas(canvasPixel, imageSize(triangleCanvasDistances))) return;
    imageAtomicMin(triangleCanvasDistances, canvasPixel, voxelDistance);
}

// Emit a face's 2x3 trixel block through the deformation matrix D.
// Super-samples by D's magnification to fill the face with no forward-
// mapping gaps. World canvas caps at n=2 (Z-yaw residual ≤ π/4 yields
// column lengths ≤ √3, since |col0|² = 3 - 2(c±s) ≤ 3 for X/Y faces);
// detached canvases cap at n=6 for full SO(3).
// At residualYaw==0 on any canvas the identity D collapses n to 1.
void emitDeformedFace(
    const ivec2 base, const mat2 D, const int voxelDistance, const int faceId,
    const bool reVoxelize
) {
    int maxN = isDetachedCanvas > 0.5 ? 6 : 2;
    int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    float inv = 1.0 / float(n);
    // Conservative coverage (#1557 Option B): a re-voxelize canvas bakes the
    // entity rotation into integer CELL positions, so round-to-cell leaves
    // sub-cell gaps between adjacent rotated cells once projected to iso. Dilate
    // each emitted surface face by ±1px along its two in-plane iso axes so the
    // gap pixels fill with the nearest face (imageAtomicMin keeps the occlusion
    // winner; stage 2's depth re-test paints the matching colour). Every other
    // canvas (world, per-axis, detached forward-scatter) emits the exact
    // footprint — byte-identical to master.
    ivec2 su = ivec2(0);
    ivec2 sv = ivec2(0);
    if (reVoxelize) {
        faceInPlaneIsoSteps(faceId, su, sv);
    }
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            vec2 src = vec2(gl_LocalInvocationID.xy) + vec2(float(sx), float(sy)) * inv;
            ivec2 p = base + roundHalfUp(D * src);
            writeDistanceTap(p, voxelDistance);
            if (reVoxelize) {
                writeDistanceTap(p + su, voxelDistance);
                writeDistanceTap(p - su, voxelDistance);
                writeDistanceTap(p + sv, voxelDistance);
                writeDistanceTap(p - sv, voxelDistance);
            }
        }
    }
}

// Fog reveal of world grid COLUMN `col` (xy world units) in [0,1]: the strongest
// live vision-circle reveal at the column center (aa = 0), or 1.0 ("fully visible")
// for explored grid memory, OOB columns, and the 1×1 placeholder. Two callers
// threshold this on the SAME analytic curve, each with the comparison its job
// needs:
//   - #2102 own-column drop: drop iff `reveal <= 0.0` (FULLY hidden — past the
//     disc's outer soft edge). Such a column's voxel would only let FOG_TO_TRIXEL
//     hard-black its faces; dropping it (STAGE_2 inherits the drop via its depth
//     re-test) shows the revealed floor / background behind instead. A PARTIALLY
//     revealed boundary column (0 < reveal < 1) is KEPT and rasterizes, so the
//     per-pixel FOG_TO_TRIXEL mask fades the object's silhouette on the SAME
//     smooth curve it fades the floor (#2126 Mode B — "reveal whole voxels then
//     smooth like the floor").
//   - #2125/#2126 neighbor cut-face test: emit a cut face iff `reveal < 1.0`
//     ("neighbor not fully revealed") so a cut face exists across the whole soft
//     boundary band for the per-pixel mask to trim.
// With a hard disc (edgeSoftness == 0, Mode A / the default) reveal is binary
// {0,1}, so `reveal <= 0.0`, `reveal < 0.5`, and `reveal < 1.0` all coincide at
// "center outside radius" — Mode A and non-fog scenes stay byte-identical.
//
// This is c_voxel_visibility_compact's drop test taken to the PRECISE radius:
// the compact keeps a permissive +edgeSoftness+1-cell margin so the per-pixel
// floor reveal stays smooth (no grid-aligned notches, #2068); STAGE_1 tightens
// that margin away for the voxel object so its fully-hidden faces no longer
// rasterize. Explored grid memory and in/at-disc columns are kept; the 1×1
// placeholder + OOB columns read as fully visible, matching the OOB-as-visible
// invariant — so non-fog / detached canvases are byte-identical.
// Duplicated byte-identically in c_voxel_to_trixel_stage_2.{glsl,metal} (fog
// grid binds on slot 3 there instead of slot 0 — slot-parameterized images are
// not possible in GLSL/MSL, hence the duplication). Keep them in sync.
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
// center rather than the cell center (#2124 screen-space cross-section) — see the
// Metal twin. The drop keeps a column iff this is > 0, so a column the disc merely
// CLIPS (center outside R, unit cell still overlaps the reveal region) is KEPT and
// rasters its full footprint; FOG_TO_TRIXEL then trims it per pixel at the exact
// analytic edge (a game-resolution silhouette) instead of the geometry ending on
// the voxel lattice (the #2102 voxel-jagged edge). Evaluating at the nearest cell
// point keeps ONLY the one-cell ring the disc crosses (tight, not a blanket
// margin); kFogColumnKeepAa is a small rim so the hard-disc smoothstep is
// non-degenerate and the AA rim / reconstruction skew never notches the edge.
const float kFogColumnCellHalf = 0.5;
const float kFogColumnKeepAa = 0.5;
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
        reveal = max(reveal, fogVisionCircleReveal(nearest, visionCircles[i], kFogColumnKeepAa));
    }
    return reveal;
}

// Neighbor-cell FULL-reveal test for the cut-face gate (#2124 analytic
// cross-section band) — the farthest-corner dual of fogColumnRevealNearest.
// Returns < 1.0 when ANY part of the cell at `col` pokes outside every vision
// circle, so the caller emits a cut face on EVERY lattice plane the disc's
// boundary band crosses — not just the plane whose neighbor CENTER is outside
// (the pre-band gate). Those interior band walls are what a view ray lands on
// after the per-micro clip below removes the outside-the-disc part of the
// nearer wall: each wall is clipped at the analytic disc in ITS OWN plane, so
// the composited silhouette is the disc's smooth screen silhouette and the
// stepped-plane sawtooth (#2180's known residual) cannot form. Extra walls
// between two fully-revealed-side cells are interior surfaces that lose the
// depth contest to the nearer exposed face — invisible, cost-only. Explored
// grid memory / OOB / the 1×1 placeholder read fully revealed (1.0), matching
// the OOB-as-visible invariant, so grid-only and non-fog scenes are unchanged.
float fogColumnRevealFarthest(ivec2 col) {
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
        const vec2 delta = vec2(col) - visionCircles[i].xy;
        const vec2 farthest = vec2(col) + vec2(
            delta.x >= 0.0 ? kFogColumnCellHalf : -kFogColumnCellHalf,
            delta.y >= 0.0 ? kFogColumnCellHalf : -kFogColumnCellHalf
        );
        reveal = max(reveal, fogVisionCircleReveal(farthest, visionCircles[i], 0.0));
    }
    return reveal;
}

// Minimum raster density for the analytic band projection: the band sits
// 2 micros inside the disc (so lattice rounding of the projected tap can
// never cross the mask's AA rim), which at coarse subdivisions becomes a
// visible cell-scale retraction of the cut. Below this density the cut
// keeps its #2180 lattice-wall behaviour (mask-trimmed) — the band engages
// exactly when zoom (effective subdivisions) makes it visible.
const int kFogBandMinSubdivisions = 4;

// Continuous-point analytic fog reveal for the per-micro clip (#2124 analytic
// cross-section band). Same guards as fogColumnReveal (placeholder / OOB /
// explored grid memory at the OWNING column read fully revealed), but the
// vision-circle curve is evaluated at the micro-face's CONTINUOUS world (x,y)
// rather than a cell-quantized point — the raster-side twin of the recovery
// FOG_TO_TRIXEL runs per pixel, so the geometry clip and the per-pixel mask
// trace the SAME analytic curve. `aa` is a half-micro conservative rim: a
// micro whose center is a hair outside but whose tile still touches the disc
// stays rasterized, and the mask owns the final sub-pixel edge.
// Duplicated byte-identically in c_voxel_to_trixel_stage_2.{glsl,metal} (fog
// grid slot differs per stage — see fogColumnReveal above). Keep in sync.
float fogPointReveal(vec2 worldXY, ivec2 col, float aa) {
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
        reveal = max(reveal, fogVisionCircleReveal(worldXY, visionCircles[i], aa));
    }
    return reveal;
}

void main() {
    uint compactedIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * numGroupsX;
    if (compactedIdx >= visibleCount) return;

    uint voxelIndex = compactedVoxelIndices[compactedIdx];
    const vec4 voxelPosition = positions[voxelIndex];

    // `slot` is the per-voxel visible-triplet index (0/1/2) — a workgroup
    // label that maps to a diamond region (right column / left column / top
    // row) and to the per-slot deformation matrix. `faceId` is the WORLD
    // FaceId (0..5) the camera sees at this slot, resolved by the CPU per
    // cardinal via `IRMath::visibleFaceTripletCardinal` (#1278).
    const int slot = localIDToFace_2x3(gl_LocalInvocationID.xy);
    const int faceId = visibleFaceIds[slot];
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);

    // Re-voxelize marker: detached canvases (visibleFaceIds.w != 0, #1557) bake
    // the entity rotation into the CELL positions and raster at cardinal 0.
    const bool reVoxelize = visibleFaceIds.w != 0;

    // Exposed-face gate (#1278): emit only when the world face this slot
    // renders is BOTH camera-visible (the slot-to-faceId resolution above
    // already guarantees this) AND exposed (neighbor cell empty). Interior
    // voxels of a solid cube emit nothing — surface area, not volume —
    // and the depth-tie ambiguity between interior +X and exterior -X
    // copies that produced the pre-#1278 stripe artifact (#1256) cannot
    // arise because the interior copy was never emitted. Bit position
    // matches `IRComponents::VoxelFlags::kFaceOccluded(faceId)`.
    //
    // Re-voxelize now gates here too. The GPU scatter (c_revoxelize_detached
    // MODE 1) authors the ROTATED-frame exposed mask from dest-grid adjacency
    // (the GPU twin of REBUILD_GRID_VOXELS' #1720 CPU mask), so `flags_` is
    // valid in the rotated frame — the old `.w`-bypass (emit all three cardinal
    // faces, let the depth re-test keep the front) existed only because that
    // mask used to be stale, and its slot-tie winner drove AO hatching on flat
    // surfaces that the GRID path never had. Bit position matches
    // `IRComponents::VoxelFlags::kFaceOccluded(faceId)`. `reVoxelize` still drives
    // the emit dilation below.
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;

    // World fog route + world-column recovery (#2125 GRID, #2127 detached;
    // per-axis #2128). The fog grid is world-space, so the cut-face + own-column
    // tests below decide "hidden" on this voxel's WORLD column. `fogActive` is the
    // single cheap (uniform-only) gate that keeps every non-fog / plain-DETACHED /
    // Z-route voxel byte-identical to master AND free of the world-column round()
    // below. It fires on the world fog route (perAxisRoute==0: the GRID world canvas
    // or a world-placed re-voxelize detached canvas, #2127) AND the X/Y per-axis
    // rotation routes (1/2, #2128 — a cut face is a vertical X/Y face that rides the
    // matching axis canvas under continuous yaw). Plain octahedral DETACHED and the
    // Z route (3, which carries no cut faces) stay on the no-fog placeholder.
    const bool fogActive = visionCircleCount > 0 && perAxisRoute <= 2 &&
        (isDetachedCanvas < 0.5 ||
         (perAxisRoute == 0 && detachedWorldReceive.w != 0.0));
    // The GRID world canvas already rasters in world space (offset 0) and the X/Y
    // per-axis canvases are non-detached (offset 0); a world-placed detached
    // re-voxelize canvas rasters its pool in the pool-centered MODEL frame, so
    // recover its world column as model + the published cell origin — the SAME
    // (model + offset) recovery c_lighting_to_trixel uses to world-sample shadow +
    // light. The re-voxelize bake is a pure translation off world (the rotation is
    // baked into the integer cells), so the model-frame face normal equals the
    // world-frame one and the neighbor column below is correct.
    ivec2 worldColumn = ivec2(0);
    if (fogActive) {
        worldColumn = ivec3(round(voxelPosition.xyz)).xy +
            (isDetachedCanvas > 0.5 ? ivec2(round(detachedWorldReceive.xy)) : ivec2(0));
    }

    // Exposed-face gate (#1278) widened with the fog CUT-FACE rule (#2125/#2127;
    // per-axis #2128). A camera-visible face normally emits only when exposed
    // (neighbor cell empty). At the fog boundary a non-exposed VERTICAL face
    // (faceId 0..3 = ±X/±Y; ±Z never cut — the vision region is a vertical cylinder)
    // becomes the object's interior cross-section wall when the solid neighbor
    // COLUMN it faces is NOT fully revealed — so a boundary-cut object caps with a
    // real interior face instead of a see-through hole. faceOutwardNormal6I(faceId).xy
    // is the neighbor column offset (±1 along X/Y; 0 for ±Z, never reached here). The
    // `fogActive` gate (#2127) extends the cut to a world-placed re-voxelize DETACHED
    // canvas via the `worldColumn` recovery above; it also carries the per-axis route
    // selection (routes 0/1/2, #2128 — under continuous yaw the cut face rides the
    // matching X (route 1) or Y (route 2) axis canvas via the `(faceId>>1)!=axis`
    // store filter below; the Z route (3) is excluded). Keeping a `perAxisRoute`
    // comparison term in `fogActive` (rather than dropping it) makes the GLSL/MSL
    // compiler schedule the non-fog per-axis store identically to the pre-#2128
    // `== 0` gate, so non-fog rotating scenes stay byte-identical (a bare-removed
    // term reshuffled the Metal per-axis tie-winner resolution). Non-fog /
    // plain-DETACHED scenes short-circuit on `fogActive` and stay byte-identical.
    // Distance (here) and colour (stage 2) MUST apply the identical predicate or the
    // cut wall's depth and colour desync — keep this block byte-identical to stage 2's.
    //
    // P2 (#2126): the cut fires when the neighbor is "not fully revealed"
    // (reveal < 1.0), so a cut face is emitted across the WHOLE soft boundary band
    // and the per-pixel FOG_TO_TRIXEL mask owns the smooth silhouette (Mode B).
    // With a hard disc (edgeSoftness == 0, Mode A / the default) reveal is binary
    // so < 1.0 collapses to the binary boundary — Mode A is byte-identical, so the
    // re-voxelize detached fog cut (#2127) keeps its merged behaviour at the default.
    bool keepFace = faceIsExposed(flagsByte, faceId);
    bool isCutFace = false;
    if (!keepFace && faceId < kFaceZNeg && fogActive) {
        // Analytic-band widening (#2124): on the single-canvas world route the
        // neighbor test is the farthest-corner variant, so a cut face exists on
        // EVERY lattice plane inside the disc's boundary band (the per-micro
        // clip below trims each to the disc; a ray that enters the disc always
        // lands on the next band wall — the smooth screen-silhouette contract
        // in fogColumnRevealFarthest's comment). The per-axis rotation routes
        // (#2128) keep the center-based gate — their store path has no
        // per-micro clip, so wider emission would only add un-trimmed walls.
        const ivec2 neighborColumn = worldColumn + faceOutwardNormal6I(faceId).xy;
        keepFace = (perAxisRoute == 0 && isDetachedCanvas < 0.5
                        ? fogColumnRevealFarthest(neighborColumn)
                        : fogColumnReveal(neighborColumn)) < 1.0;
        isCutFace = keepFace;
    }
    if (!keepFace) return;

    // Per-voxel analytic fog clip (#2102 + #2126 P2 + #2127; per-axis split #2128).
    // On the single-canvas world fog route (perAxisRoute==0), drop a voxel whose OWN
    // world column is FULLY hidden (reveal <= 0, past the disc's outer soft edge) so
    // FOG_TO_TRIXEL can't hard-black its faces (the revealed floor / background behind
    // shows through instead); the cut faces above cap the revealed half. A PARTIALLY
    // revealed boundary column (0 < reveal < 1) is KEPT so it rasterizes and
    // FOG_TO_TRIXEL fades the object's silhouette on the same smooth curve as the
    // floor (Mode B). At edgeSoftness 0 (Mode A / default) reveal is binary, so this
    // is exactly the #2125 "drop outside radius". The per-axis rotation routes run the
    // SAME reveal<=0 clip inside the per-axis branch below (#2128) rather than here,
    // so the shared pre-split code stays byte-identical to the pre-#2128 per-axis
    // store (a perAxisRoute!=0 early-return here reshuffles the compiler's per-axis
    // tie-winner resolution); the explicit perAxisRoute==0 term keeps this an
    // unconditional no-op on routes 1/2/3. `fogActive` gates the world GRID canvas +
    // world-placed re-voxelize detached canvas (#2127) and short-circuits every other
    // route, keeping non-fog scenes byte-identical.
    if (fogActive && perAxisRoute == 0 && fogColumnRevealNearest(worldColumn) <= 0.0) {
        return;
    }

    // At cardinalIndex==0 the rotation is the identity; gating it behind a
    // branch keeps the GLSL/MSL compilers from reshuffling instructions or
    // changing depth-tie ordering on the GPU, so yaw=0 stays byte-identical
    // pixel-for-pixel against master.

    // Per-slot deformation matrix — `D` shapes the diamond corner offsets
    // under residualYaw and (for detached canvas) per-face SO(3). At cardinal
    // 0 + residualYaw==0 every slot's D is the identity, so the per-slot
    // path collapses to faceOffset_2x3(slot, subPixel) — bit-identical
    // pixel positions against the pre-T-293 path.
    const mat2 D = mat2(faceDeform[slot].xy, faceDeform[slot].zw);

    // Smooth camera Z-yaw per-axis routing (T2 / #1309 + T3 / #1310;
    // docs/design/per-axis-trixel-canvas-rotation.md). At perAxisRoute==0 this
    // is skipped and the single-canvas path below runs unchanged (byte-
    // identical to master). At perAxisRoute 1/2/3 we are rasterizing the X/Y/Z
    // axis canvas: emit ONLY the visible face on that axis, reposition its
    // center *continuously* with pos3DtoPos2DIsoYawed (replacing the
    // rotateCardinalZ integer snap, so centers swing smoothly between
    // cardinals), and write the shared world-space depth pos3DtoDistance —
    // identical across all three axis canvases so the framebuffer composite
    // can pick the nearest.
    //
    // T3 (#1310, Option-4 forward scatter): store ONE cell per face center
    // (not the emitDeformedFace super-sampled cluster T2 wrote). The cell sits
    // at the voxel's continuously-yawed iso position; `atomicMin` resolves
    // voxel-vs-voxel occlusion per cell (nearest face on this view ray wins),
    // so every non-empty cell is exactly one occlusion-winning face. The
    // framebuffer scatter (system_trixel_to_framebuffer) then forward-projects
    // each non-empty cell as its true deformed face quad — recovering the
    // world origin from (cell - perAxisBase, depth>>2, visualYaw) — with no
    // gather/parity inverse, so the #1256 stripe class cannot occur. The face
    // SHAPE is reconstructed at scatter time, so the per-slot deform D is no
    // longer applied here.
    if (perAxisRoute != 0) {
        // Per-axis own-column fog clip (#2128): the same #2102 + #2126 P2 drop as
        // the single-canvas route above (reveal <= 0 — FULLY hidden), applied on
        // EVERY axis route (1/2/3) so a rotating boundary object clips its hidden
        // half identically (a hidden column's Z face would otherwise float on route
        // 3). Lives inside the per-axis branch so the shared pre-split code is
        // byte-identical to the pre-#2128 per-axis store; visionCircleCount==0 /
        // the 1×1 placeholder short-circuit, so non-fog rotating scenes stay
        // byte-identical.
        if (visionCircleCount > 0 &&
            fogColumnReveal(ivec3(round(voxelPosition.xyz)).xy) <= 0.0) {
            return;
        }
        const int axis = perAxisRoute - 1;
        if ((faceId >> 1) != axis) return;
        // Un-yawed (cardinal) iso store: key each face by its cardinal iso pixel
        // `perAxisBase + pos3DtoPos2DIso(facePos)` rather than the in-plane
        // (y,z)/(x,z)/(x,y) lattice. The in-plane lattice collapses faces sharing
        // an in-plane column but differing in depth-along-the-fixed-axis (separate
        // objects stacked along the axis) onto one cell -> the back face is
        // dropped even though it is screen-separated. The cardinal iso key depends
        // on all three coords, so screen-separated faces land in distinct cells
        // and both survive; collisions occur only for genuine same-pixel cardinal
        // occlusion (resolved by the rawDepth atomicMin). This is NOT the yawed
        // iso store #1310 fled (compressed-axis collapse + singular inverse): the
        // index is UN-yawed, so no axis is compressed at store time and the
        // recovery `isoPixelToPos3D` is exact at every yaw. The scatter reprojects
        // the recovered origin under the live yaw.
        // Whole-iso base anchor (#1944): the per-axis store is BASE-resolution, so
        // the anchor must NOT be density-scaled like the subdivided cardinal canvas
        // (the density-scaled anchor jittered under pan — see the #1944 NOTE in
        // ir_iso_common). The cardinal single-canvas paths below keep
        // trixelFrameOffset (their content IS subdivided).
        const ivec2 perAxisBase = trixelOriginOffsetZ1(canvasSizePixels) + ivec2(floor(frameCanvasOffset));
        if (voxelRenderOptions.x == 0) {
            const ivec3 worldPos = ivec3(round(voxelPosition.xyz));
            const ivec3 facePos = faceMicroPositionFixed6(faceId, worldPos, 0, 0, 1);
            // No sub-cell offset at base resolution; encode centre fracs (8,8).
            const int voxelDistance =
                encodeDepthWithFaceFrac(pos3DtoDistance(facePos), slot, 8, 8);
            writeDistanceTap(perAxisBase + pos3DtoPos2DIso(facePos), voxelDistance);
            return;
        }
        // #1458: store at BASE (world-unit) resolution regardless of effSub.
        // Only the z=0 invocation writes; higher z-slices return early.
        // The voxel's continuous sub-cell offset is packed into the lower bits
        // of the encoding so scatter can sub-pixel-shift the face quad.
        if (gl_WorkGroupID.z != 0) return;
        const vec3 worldAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
        const ivec3 worldPos_sub = ivec3(round(worldAligned));
        const ivec3 facePos_sub = faceMicroPositionFixed6(faceId, worldPos_sub, 0, 0, 1);
        const vec3 fracInCell = worldAligned - vec3(worldPos_sub);
        const int voxelDistance =
            encodeDepthWithFaceFrac(pos3DtoDistance(facePos_sub), slot, axis, fracInCell);
        writeDistanceTap(perAxisBase + pos3DtoPos2DIso(facePos_sub), voxelDistance);
        return;
    }

    if (voxelRenderOptions.x == 0) {
        ivec3 voxelPositionInt = ivec3(round(voxelPosition.xyz));
        if (cardinalIndex != 0) {
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
            voxelPositionInt += cardinalLowerCornerShift(cardinalIndex);
        }
        // Encode `slot` (not faceId) in depth — keeps the 2-bit field
        // unchanged, and AO/lighting recover the world faceId via
        // visibleFaceIds[slot] from the same UBO.
        // Detached entities raster in model space (camera yaw zeroed), so the
        // occlusion order projects onto the entity-rotated iso axis (#1462).
        // The world canvas keeps the fixed (1,1,1) via pos3DtoDistance, so its
        // depth is byte-identical to master (voxelPositionInt is integer, so
        // the detached path's roundHalfUp(dot(pos,(1,1,1))) would equal x+y+z
        // at identity too — the branch only guards the GRID fast path).
        const int rawDepth = isDetachedCanvas > 0.5
            ? isoDepthAlongAxis(voxelPositionInt, voxelDepthAxis.xyz)
            : pos3DtoDistance(voxelPositionInt);
        const int voxelDistance = encodeDepthWithFace(rawDepth, slot);
        const ivec2 base =
            trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions) +
            pos3DtoPos2DIso(voxelPositionInt);
        emitDeformedFace(base, D, voxelDistance, faceId, reVoxelize);
        return;
    }

    const int subdivisions = max(voxelRenderOptions.y, 1);
    int u = int(gl_WorkGroupID.z) / subdivisions;
    int v = int(gl_WorkGroupID.z) % subdivisions;

    const vec3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const ivec3 voxelPositionFixed = ivec3(round(voxelPositionAligned * float(subdivisions)));
    const ivec2 frameOffsetFixed =
        trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);

    // Six-face micro position — POS faces start at `voxelPositionFixed.<axis>
    // + subdivisions` (high-coordinate side), NEG faces at
    // `voxelPositionFixed.<axis>` (low-coordinate side, matching the 3-face
    // path bit-for-bit at cardinal 0).
    ivec3 microPositionFixed =
        faceMicroPositionFixed6(faceId, voxelPositionFixed, u, v, subdivisions);
    // Per-micro analytic fog clip (#2124 analytic cross-section band; WORLD
    // canvas only). Skip a face micro whose CONTINUOUS world column is past
    // the disc's outer edge — evaluated at the micro-face center on the
    // PRE-rotation fixed lattice (the raster geometry FOG_TO_TRIXEL's
    // per-pixel recovery inverts). Clipping at raster (not the post-mask)
    // lets the ray reveal the NEXT band wall behind (imageAtomicMin), so the
    // cross-section composites the disc's smooth screen silhouette at micro
    // (1/sub world) depth quantization instead of hard-blacking whole-face
    // overhangs — the vertical dual of the top-face mask arc. Half-micro `aa`
    // keeps boundary micros conservatively; the mask owns the final sub-pixel
    // edge. The in-plane +0.5 recenters onto the micro-tile center per faceId
    // axis (u/v span the two non-face axes — see faceMicroPositionFixed6).
    // A world-placed detached re-voxelize canvas is EXCLUDED (its tight
    // model-frame canvas clips displaced band taps at its bounds and its
    // capped subdivision coarsens the micro test) — it keeps the merged
    // #2127 keep-and-mask behaviour; extending the band there needs
    // canvas-margin awareness (follow-up).
    // MUST stay byte-identical to stage 2's twin block or the cut wall's
    // depth and colour desync.
    if (fogActive && isDetachedCanvas < 0.5) {
        const int faceAxis = faceId >> 1;
        vec2 microCenterFixed = vec2(microPositionFixed.xy);
        if (faceAxis != 0) microCenterFixed.x += 0.5;
        if (faceAxis != 1) microCenterFixed.y += 0.5;
        const vec2 microWorldXY = microCenterFixed / float(subdivisions);
        if (fogPointReveal(microWorldXY, worldColumn, 0.5 / float(subdivisions)) <= 0.0) {
            return;
        }
        // Analytic band projection (#2124): slide a cut-face micro radially
        // OUTWARD onto the disc surface (radius − two micros, so lattice
        // rounding of the projected tap can never cross the per-pixel mask's
        // AA rim and re-darken alternate taps). The lattice cut walls stand up
        // to a cell behind the true cylindrical cut, so their bottoms/corners
        // step at cell pitch; re-homing every band micro onto the disc collapses
        // the steps into ONE smooth surface. Outward travel stays inside the
        // solid fog-hidden neighbor the cut faces (≤ 1.5 cells, clamped) — a
        // longer projection means this wall is not the boundary wall and keeps
        // its lattice position, as do walls cut by explored-grid memory or a
        // soft-edged disc (Mode B keeps its faded lattice cross-section).
        if (isCutFace && subdivisions >= kFogBandMinSubdivisions) {
            const float microHalf = 1.0 / float(subdivisions);
            float bestMargin = 1e9;
            int bestCircle = -1;
            for (int i = 0; i < visionCircleCount; ++i) {
                if (visionCircles[i].w != 0.0) continue; // hard discs only (Mode A)
                const float margin =
                    visionCircles[i].z - length(microWorldXY - visionCircles[i].xy);
                if (margin > 0.0 && margin < bestMargin) {
                    bestMargin = margin;
                    bestCircle = i;
                }
            }
            if (bestCircle >= 0 && bestMargin > 2.0 * microHalf &&
                bestMargin <= 1.5 + 2.0 * microHalf) {
                // Band shading slot: the band's true normal is the radial
                // outward direction, so shade EVERY band pixel with the
                // visible vertical face whose outward normal is nearest to it
                // (per 45° sector). Without this, band pixels inherit the slot
                // of whichever source wall (−X vs −Y family) won the pixel and
                // the two Lambert families interleave as stripes.
                const vec2 radial = microWorldXY - visionCircles[bestCircle].xy;
                const int xSlot = (visibleFaceIds[0] >> 1) == 0
                    ? 0 : (((visibleFaceIds[1] >> 1) == 0) ? 1 : 2);
                const int ySlot = (visibleFaceIds[0] >> 1) == 1
                    ? 0 : (((visibleFaceIds[1] >> 1) == 1) ? 1 : 2);
                const int bandSlot = abs(radial.x) >= abs(radial.y) ? xSlot : ySlot;
                // Coverage backstop: also emit the UN-projected lattice tap.
                // The projected band is seen near edge-on at the silhouette
                // wrap-around, where a forward map inevitably leaves pixel
                // gaps (the scatter-dropout class); the lattice wall sits ≤ 1
                // cell BEHIND the band with the same colour + band shading
                // slot, so band gaps resolve to it invisibly.
                ivec3 latticeTap = microPositionFixed;
                if (cardinalIndex != 0) {
                    latticeTap = rotateCardinalZ(latticeTap, cardinalIndex);
                    latticeTap += cardinalLowerCornerShift(cardinalIndex) * subdivisions;
                }
                // World canvas only here (block gate), so depth is the plain
                // (x+y+z) iso sum — no detached depth-axis branch.
                const int latticeDepth = latticeTap.x + latticeTap.y + latticeTap.z;
                emitDeformedFace(
                    frameOffsetFixed + pos3DtoPos2DIso(latticeTap), D,
                    encodeDepthWithFace(latticeDepth, bandSlot), faceId, reVoxelize
                );
                // The band tap itself, radially re-homed onto the disc.
                const float target = visionCircles[bestCircle].z - 2.0 * microHalf;
                const vec2 surfFixedXY =
                    (visionCircles[bestCircle].xy +
                     radial * (target / length(radial))) *
                    float(subdivisions);
                ivec3 bandTap = ivec3(roundHalfUp(surfFixedXY), microPositionFixed.z);
                if (cardinalIndex != 0) {
                    bandTap = rotateCardinalZ(bandTap, cardinalIndex);
                    bandTap += cardinalLowerCornerShift(cardinalIndex) * subdivisions;
                }
                const int bandDepth = bandTap.x + bandTap.y + bandTap.z;
                emitDeformedFace(
                    frameOffsetFixed + pos3DtoPos2DIso(bandTap), D,
                    encodeDepthWithFace(bandDepth, bandSlot), faceId, reVoxelize
                );
                return;
            }
        }
    }
    if (cardinalIndex != 0) {
        microPositionFixed = rotateCardinalZ(microPositionFixed, cardinalIndex);
        // Shift is per-world-unit; scale to subdivision units to match
        // `voxelPositionFixed = round(worldPos * subdivisions)`.
        microPositionFixed += cardinalLowerCornerShift(cardinalIndex) * subdivisions;
    }
    // Detached entities project occlusion depth onto the entity-rotated iso
    // axis (#1462); world/GRID keeps the (x+y+z) fixed-(1,1,1) form. Depth is
    // in subdivision units on both branches, so the encode scale is unchanged.
    const int depthBase = isDetachedCanvas > 0.5
        ? isoDepthAlongAxis(microPositionFixed, voxelDepthAxis.xyz)
        : (microPositionFixed.x + microPositionFixed.y + microPositionFixed.z);
    const int voxelDistance = encodeDepthWithFace(depthBase, slot);
    const ivec2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(base, D, voxelDistance, faceId, reVoxelize);
}
