/*
 * Project: Irreden Engine
 * File: c_voxel_to_trixel_stage_1_body.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Shared stage-1 compute body: an include fragment, not a standalone shader. Three wrappers
// compile it:
//   c_voxel_to_trixel_stage_1.glsl        → FEEDER 0, ELECTION 0 = visible dispatch
//   c_voxel_to_trixel_stage_1_feeder.glsl → FEEDER 1, ELECTION 0 = shadow-feeder dispatch
//   c_voxel_to_trixel_stage_1_winner_resolve.glsl
//                                         → FEEDER 0, ELECTION 1 = cardinal
//     winner-election dispatch
// Each wrapper defines IR_FEEDER_PASS, IR_STORE_WINNER_ELECTION and IR_VOXEL_FOG_GRID_BINDING,
// then includes, in this order: ir_iso_common.glsl, ir_constants.glsl,
// ir_voxel_face_select.glsl, this body. The body carries no `#include` of its own because its
// prerequisite chain is macro-parameterized (ir_voxel_face_select.glsl reads
// IR_VOXEL_FOG_GRID_BINDING; this body reads IR_FEEDER_PASS / IR_STORE_WINNER_ELECTION), and a
// self-include could resolve a macro-consuming file above the wrapper's `#define`.
// Variant-only code sits under `#if IR_FEEDER_PASS` / `#if IR_STORE_WINNER_ELECTION` so the
// visible kernel compiles with both variants' branches textually absent: a runtime uniform
// branch would be predicated on every invocation of the hottest kernel.

// local_size_z MUST equal kStageMicroSlicesPerGroup (ir_constants.glsl). It stays a literal
// because a compute-shader layout qualifier needs a literal on every GL driver; the shared
// constant drives the micro-slice math and guard.
layout(local_size_x = 2, local_size_y = 3, local_size_z = 8) in;

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
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single-canvas raster;
    // 1/2/3 = the X/Y/Z per-axis canvas pass.
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;         // trixel canvas dimensions
    uniform ivec2 cullIsoMin;               // iso-space cull viewport (matches CPU chunk mask)
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;                // continuous Z-yaw (radians)
    uniform float rasterYaw;                // cardinal-snap multiple of pi/2 nearest visualYaw
    // visualYaw - rasterYaw, in [-pi/4, pi/4]. faceDeform[] absorbs it during
    // the trixel emit.
    uniform float residualYaw;
    // 1.0 for a detached entity canvas, 0.0 for the world canvas. Gates
    // emitDeformedFace max super-sample level (world: 2, detached: 6).
    uniform float isDetachedCanvas;
    // Per-slot deformation matrix packed column-major into vec4: .xy = col0,
    // .zw = col1 of IRMath::faceDeformationMatrix(axis(visibleFaceIds[slot]),
    // residualYaw). **Indexed by visible-triplet SLOT (0/1/2)**, not by axis —
    // at non-zero cardinal the WORLD face whose matrix lives at slot s
    // changes per `visibleFaceIds[s]`. Identity at residualYaw==0.
    uniform vec4 faceDeform[3];
    // Per-slot world FaceId (0..5 = X_NEG/X_POS/Y_NEG/Y_POS/Z_NEG/Z_POS) —
    // the three camera-visible faces resolved by
    // `IRMath::visibleFaceTripletCardinal` on the CPU. Slot 0/1/2 map to the
    // workgroup-local face slot returned by `localIDToFace_2x3`; `.w` != 0
    // marks a re-voxelize detached canvas. At cardinal 0 the default
    // {0, 2, 4} = {X_NEG, Y_NEG, Z_NEG}.
    uniform ivec4 visibleFaceIds;
    // Model-frame iso depth axis `R⁻¹·(1,1,1)` for the per-voxel occlusion
    // metric. (1,1,1) for the world canvas / identity entity, so
    // isoDepthAlongAxis collapses to pos3DtoDistance; a rotated DETACHED
    // canvas uploads `IRMath::isoDepthAxisModel(rotation)`. `.w` is std140
    // padding. std140 offset 144.
    uniform vec4 voxelDepthAxis;
    // World-receive offset for a world-placed detached re-voxelize solid.
    // `.xyz` = the entity's world cell origin (`roundVec3HalfUp(translation)`,
    // the SAME offset c_lighting_to_trixel adds to world-sample shadow + light);
    // `.w` = 1.0 when world-placed, else 0.0. The detached re-voxelize canvas
    // rasters its pool in the pool-centered MODEL frame, so the fog cut-face +
    // own-column tests recover each voxel's WORLD column as
    // `roundHalfUp(voxelPosition.xy) + roundHalfUp(.xy)`
    // and decide "hidden" against the shared world fog grid. std140 offset 160,
    // matching FrameDataVoxelToCanvas; the all-zero value leaves the world /
    // per-axis / screen-locked canvases unaffected.
    uniform vec4 detachedWorldReceive;
    // Un-widened iso cull viewport for the depth-only shadow-feeder path.
    // Read by stage 2 only; declared here so resolveMode lands at the same
    // std140 offset (192) as FrameDataVoxelToCanvas::resolveMode_.
    uniform ivec4 visibleIsoBounds;
    // Per-axis deterministic-winner resolve mode. 0 = the distance store (also
    // writes the view mask). 1 = the winner-resolve dispatch between the stage-1
    // store and stage 2: re-run the identical per-axis geometry and, for each
    // face whose encoded distance MATCHES the settled per-cell atomicMin
    // winner, atomicMin the face's run-stable voxel pool index into the
    // per-cell winner scratch — so stage 2's color/entity-id tap admits
    // exactly one of the equal-key faces (the minimum index). 3 = the overflow
    // append. Only ever non-zero during the per-axis dispatches
    // (perAxisRoute != 0).
    uniform int resolveMode;
    // Per-voxel Hi-Z occlusion-cull gate, read by the compact only — declared
    // here so the feeder lanes land at their real std140 offsets (200/204)
    // behind FrameDataVoxelToCanvas::occlusionCullMipCount_ at 196.
    uniform int occlusionCullMipCount;
    // Shadow-feeder dispatch partition. Which pass this kernel is (visible vs
    // feeder) is the compile-time `IR_FEEDER_PASS`, not a uniform. The feeder
    // variant reads from the TAIL of the compacted buffer at
    // feederPassTailBase-1-compactedIdx and rasters a STRIDED micro-grid capped
    // to feederSubCap per face edge (feederSubCap² cells vs effSub²); the
    // visible variant never touches these two fields. Feeders are off-screen
    // shadow casters — their coarser trixel depth feeds only the sun-shadow
    // bake, never an on-screen pixel. feederSubCap == effSub rasters feeders at
    // full visible density. Both lanes stay in the std140 block (offsets
    // 200/204) so every binding-7 shader shares one layout.
    uniform int feederSubCap;
    uniform int feederPassTailBase;
    // View-visibility overflow scratch layout: region base offsets (in uints)
    // into the unified binding-28 scratch + the entry cap. .x = view mask,
    // .y = ctrl block (draw args + counters), .z = overflow entries, .w = entry
    // cap. Region 0 of the scratch is the winner-id array, indexed directly as
    // perAxisWinnerIds[cell]. std140 offset 208, mirroring
    // FrameDataVoxelToCanvas::overflowScratchLayout_. Read at resolveMode 0
    // (mask write) and 3 (append), rotating frames only.
    uniform ivec4 overflowScratchLayout;
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

layout(std430, binding = 6) readonly buffer ColorBuffer {
    Voxel voxels[];
};

// Face-occlusion bit indices live at `2 + faceId` in `materialFlagBone`'s
// byte 5, mirroring `IRComponents::VoxelFlags::kFaceOccluded*` in
// engine/prefabs/irreden/voxel/components/component_voxel.hpp. The
// exposed-face test (visible-triplet × exposed-mask) is centralized in
// `faceIsExposed(flagsByte, faceId)` from ir_iso_common.glsl.

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

// Per-cell deterministic-winner scratch: the winner-resolve dispatches
// atomicMin the depth-winning faces' run-stable voxel pool indices here (reset
// to 0xFFFFFFFF by the CPU), and stage 2's winner-guarded tap writes
// color/entity-id only for the winning index. A buffer (not a texture image)
// because Metal has no second image-atomic slot; it transiently reuses
// kBufferIndex_PerAxisResolveScratch. The winner-id region (region 0) stays
// untouched at resolveMode == 0, but the mode-0 per-axis store writes the
// disjoint view-mask region (base overflowScratchLayout.x) through viewMaskTap.
// Only the per-axis dispatches and the IR_STORE_WINNER_ELECTION variant write
// it; the visible and feeder cardinal / detached paths leave it untouched.
layout(std430, binding = 28) buffer PerAxisWinnerScratch {
    uint perAxisWinnerIds[];
};

// The fog grid, the observer block (including visionCircleHeights, which only
// this body's Z-cost drop reads — a named uniform block admits one declaration),
// fogColumnReveal/Nearest and the shared face-selection / per-axis store-key
// math come from ir_voxel_face_select.glsl. The fog grid takes image slot 0 via
// IR_VOXEL_FOG_GRID_BINDING; the distance image is slot 1.

void writeDistanceTap(const ivec2 canvasPixel, const int voxelDistance) {
    if (!isInsideCanvas(canvasPixel, imageSize(triangleCanvasDistances))) return;
    imageAtomicMin(triangleCanvasDistances, canvasPixel, voxelDistance);
}

// Winner-resolve tap (resolveMode == 1): among the faces whose encoded
// distance equals the settled atomicMin winner at this cell, elect the
// smallest run-stable voxel pool index. Exactly one face per voxel emits per
// axis route (the `(faceId>>1) != axis` filter), so voxelIndex is unique per
// per-axis tap and the election is a total order — stage 2's matching guard
// then admits exactly one writer, making the color/entity-id planes
// order-independent like the distance plane's atomicMin.
// The cardinal election variant (IR_STORE_WINNER_ELECTION 1) reuses this
// tap at every cardinal-branch distance-tap site: a voxel can tap a cell more
// than once there (a slot's 2x3 block, the re-voxelize dilation, the dual
// emit), but all same-voxel taps that match one settled key carry identical
// color/entity-id values, so stage 2's guarded writes stay order-independent.
void resolveWinnerTap(const ivec2 canvasPixel, const int voxelDistance, const uint voxelIndex) {
    const ivec2 canvasSize = imageSize(triangleCanvasDistances);
    if (!isInsideCanvas(canvasPixel, canvasSize)) return;
    if (imageLoad(triangleCanvasDistances, canvasPixel).x != voxelDistance) return;
    const uint cell = uint(canvasPixel.y) * uint(canvasSize.x) + uint(canvasPixel.x);
    atomicMin(perAxisWinnerIds[cell], voxelIndex);
}

// View-visibility overflow lane — yawed-depth quantization shared by the mask
// write (in the mode-0 store) and the resolveMode-3 mask compare. 1/16-world-unit
// steps, biased to a uint so atomicMin orders negative depths correctly. Both
// modes call THE SAME function on the SAME facePos, so a face always ties its own
// mask entry exactly regardless of float rounding.
const float kOverflowDepthQuantScale = 16.0;
// Half a world unit of tolerance (8 sixteenth-steps): absorbs quantization
// ties between genuinely co-visible faces without admitting occluded coset
// losers (the nearest coset pair separates by >= ~2.7 world units of yawed
// depth). Over-emit is safe — the framebuffer depth test cleans up; under-emit
// leaves holes where a view-visible face is missing.
const uint kOverflowDepthEpsSteps = 8u;
const int kOverflowDepthBias = 0x40000000;

uint overflowYawedDepthKey(const ivec3 facePos) {
    return uint(
        int(floor(yawedIsoDistanceCellAnchor(vec3(facePos), visualYaw) * kOverflowDepthQuantScale)) +
        kOverflowDepthBias
    );
}

// The face's screen cell at the LIVE yaw, on the same perAxisBase anchor the
// cardinal store uses (the scatter projects with the identical cell-anchor
// projection, so mask cells and scattered quads agree).
ivec2 overflowYawedPixel(const ivec2 perAxisBase, const ivec3 facePos) {
    return perAxisBase + roundHalfUp(pos3DtoPos2DIsoYawedCellAnchor(vec3(facePos), visualYaw));
}

// View-mask write, run inside the resolveMode-0 store pass. Every per-axis face
// (all three axis routes — view visibility competes across axes) atomicMins its
// quantized yawed depth into the shared mask region.
void viewMaskTap(const ivec2 perAxisBase, const ivec3 facePos) {
    const ivec2 yawedPix = overflowYawedPixel(perAxisBase, facePos);
    if (!isInsideCanvas(yawedPix, canvasSizePixels)) return;
    const uint cell = uint(yawedPix.y) * uint(canvasSizePixels.x) + uint(yawedPix.x);
    atomicMin(
        perAxisWinnerIds[uint(overflowScratchLayout.x) + cell],
        overflowYawedDepthKey(facePos)
    );
}

// resolveMode == 3: overflow append. A face appends iff it is view-visible
// (within epsilon of its view-mask cell winner) AND it is NOT its cardinal
// store cell's settled winner — exactly the set `viewVisible \ cardinalWinners`
// the cardinal-keyed store drops. Entries carry the exact (cardinal cell,
// encoded distance) pair the store would have written plus the raw colorPacked,
// so the scatter's overflow branch reuses the per-cell recovery bit-for-bit.
void overflowAppendTap(
    const ivec2 perAxisBase, const ivec3 facePos, const int voxelDistance, const uint colorPacked
) {
    // Compare the face's key against the MOST PERMISSIVE (largest) mask winner
    // over the 2x2 cell neighborhood spanning the UNROUNDED yawed position, not
    // the single roundHalfUp cell. A face whose footprint straddles a cell
    // boundary rounds to cell A at one yaw step and the adjacent cell B at the
    // next; a single-cell compare would flip its append membership discretely
    // (A and B carry different winners), popping a whole face quad
    // frame-to-frame. roundHalfUp(p) is floor(p) or floor(p)+1 per axis, so both
    // A and B always lie in the 2x2 neighborhood of the unrounded position;
    // reading the neighborhood max makes the compare vary continuously with the
    // winner landscape the footprint actually covers. A 3x3 span buys nothing:
    // the sub-pixel residual that survives is the per-axis scatter's positioning
    // wobble, not overflow membership. The mask WRITE side (viewMaskTap) stays
    // the single roundHalfUp cell, so the write/compare self-tie holds: a face's
    // own rounded cell is inside its neighborhood, and max() can only admit a
    // superset of the single-cell pass — the safe over-emit direction (over-emit
    // loses the framebuffer depth test; under-emit leaves holes).
    const vec2 yawedPosRel = pos3DtoPos2DIsoYawedCellAnchor(vec3(facePos), visualYaw);
    const ivec2 neighborhoodBase = perAxisBase + ivec2(floor(yawedPosRel));
    bool anyInside = false;
    uint maxMaskKey = 0u;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            const ivec2 neighborPix = neighborhoodBase + ivec2(dx, dy);
            if (!isInsideCanvas(neighborPix, canvasSizePixels)) continue;
            const uint neighborCell = uint(neighborPix.y) * uint(canvasSizePixels.x) + uint(neighborPix.x);
            maxMaskKey = max(maxMaskKey, perAxisWinnerIds[uint(overflowScratchLayout.x) + neighborCell]);
            anyInside = true;
        }
    }
    if (!anyInside) return; // off-screen at the live yaw (whole footprint off-canvas)
    // Wrap-safe occlusion test: an unwritten neighborhood cell reads the
    // 0xFFFFFFFF empty sentinel, so `maxMaskKey + eps` would wrap — compare in the
    // `key - eps` form instead (the key is bias-centered at ~0x40000000, eps=8u,
    // so no underflow), which treats the empty sentinel as infinitely permissive
    // (a footprint straddling background is on the silhouette — append it).
    if (overflowYawedDepthKey(facePos) - kOverflowDepthEpsSteps > maxMaskKey) {
        return; // view-occluded — nearer faces own the whole footprint neighborhood
    }
    const ivec2 cardPix = perAxisBase + pos3DtoPos2DIso(facePos);
    // Off-canvas cardinal key never stored (writeDistanceTap dropped it) and is
    // outside the worst-case-sized render domain — mirror the silent drop.
    if (!isInsideCanvas(cardPix, imageSize(triangleCanvasDistances))) return;
    if (imageLoad(triangleCanvasDistances, cardPix).x == voxelDistance) {
        return; // this face IS (or ties) the settled winner — the cell path draws it
    }
    const uint ctrlBase = uint(overflowScratchLayout.y);
    const uint idx = atomicAdd(perAxisWinnerIds[ctrlBase + 1u], 1u);
    if (idx >= uint(overflowScratchLayout.w)) {
        // Cap hit: pair the add back off so instanceCount settles at exactly
        // min(appends, cap), and count the drop for the CPU one-shot warn
        // (never silent). No reader sees the transient over-cap value — the
        // indirect draw is barriered behind this whole dispatch.
        atomicAdd(perAxisWinnerIds[ctrlBase + 1u], 0xFFFFFFFFu); // == -1 (wraps)
        atomicAdd(perAxisWinnerIds[ctrlBase + 5u], 1u);
        return;
    }
    const uint entryBase = uint(overflowScratchLayout.z) + idx * 3u;
    perAxisWinnerIds[entryBase + 0u] =
        (uint(cardPix.x) & 0xFFFFu) | ((uint(cardPix.y) & 0xFFFFu) << 16u);
    perAxisWinnerIds[entryBase + 1u] = colorPacked;
    perAxisWinnerIds[entryBase + 2u] = uint(voxelDistance);
}

// Emit a face's 2x3 trixel block through the deformation matrix D.
// Super-samples by D's magnification to fill the face with no forward-
// mapping gaps. World canvas caps at n=2 (Z-yaw residual ≤ π/4 yields
// column lengths ≤ √3, since |col0|² = 3 - 2(c±s) ≤ 3 for X/Y faces);
// detached canvases cap at n=6 for full SO(3).
// At residualYaw==0 on any canvas the identity D collapses n to 1.
// KEEP IN SYNC with c_voxel_visibility_compact.{glsl,metal} voxelOccludedByHiZ:
// the per-voxel Hi-Z occlusion cull's sampled window MUST be a conservative
// superset of this function's write set (`base + roundHalfUp(D * src)` for src
// across the [0,2)x[0,3) invocation lattice), so a visible voxel's own last-frame
// depth write always lands in the window it is tested against. Widening this
// emission hull (a larger super-sample lattice, a new dilation, a bigger D)
// without widening the compact's window false-culls voxels whose write escapes
// the stale window (static-scene silhouette holes).
// Under IR_STORE_WINNER_ELECTION every distance tap in this function becomes a
// resolveWinnerTap, so the election dispatch's footprint equals the store's by
// construction — a missed site would leave winner == 0xFFFFFFFF at a tapped
// pixel and stage 2's guard would reject ALL writers there (a colour hole).
// KEEP IN SYNC with kGpuMargin, the shadow-feeder classify margin in
// system_voxel_to_trixel.hpp: stage 2's depth-only feeder skip is safe only
// because this write set stays INSIDE that margin. On the cardinal world route
// (identity D, n == 1) the set is base + {0,1}x{0,1,2} — reach +1 texel in x,
// +2 in y, 0 toward -x/-y — against a 4-texel margin. Widening the hull widens
// this obligation too; overrunning THIS bound makes an on-screen pixel resolve
// from a voxel whose colour tap stage 2 skipped. Gate:
// scripts/feeder-margin-verify.py.
void emitDeformedFace(
    const ivec2 base, const mat2 D, const int voxelDistance, const int faceId,
    const bool reVoxelize
#if IR_STORE_WINNER_ELECTION
    , const uint voxelIndex
#endif
) {
    int maxN = isDetachedCanvas > 0.5 ? 6 : 2;
    int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    float inv = 1.0 / float(n);
    // Conservative coverage: a re-voxelize canvas bakes the entity rotation
    // into integer CELL positions, so round-to-cell leaves sub-cell gaps
    // between adjacent rotated cells once projected to iso. Each emitted surface
    // face dilates by ±1px along its two in-plane iso axes so the gap pixels
    // fill with the nearest face (imageAtomicMin keeps the occlusion winner;
    // stage 2's depth re-test paints the matching colour). Every other canvas
    // (world, per-axis, detached forward-scatter) emits the exact footprint.
    ivec2 su = ivec2(0);
    ivec2 sv = ivec2(0);
    if (reVoxelize) {
        faceInPlaneIsoSteps(faceId, su, sv);
    }
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            vec2 src = vec2(gl_LocalInvocationID.xy) + vec2(float(sx), float(sy)) * inv;
            ivec2 p = base + roundHalfUp(D * src);
#if IR_STORE_WINNER_ELECTION
            resolveWinnerTap(p, voxelDistance, voxelIndex);
            if (reVoxelize) {
                resolveWinnerTap(p + su, voxelDistance, voxelIndex);
                resolveWinnerTap(p - su, voxelDistance, voxelIndex);
                resolveWinnerTap(p + sv, voxelDistance, voxelIndex);
                resolveWinnerTap(p - sv, voxelDistance, voxelIndex);
            }
#else
            writeDistanceTap(p, voxelDistance);
            if (reVoxelize) {
                writeDistanceTap(p + su, voxelDistance);
                writeDistanceTap(p - su, voxelDistance);
                writeDistanceTap(p + sv, voxelDistance);
                writeDistanceTap(p - sv, voxelDistance);
            }
#endif
        }
    }
}

// Z-cost twins of fogColumnReveal / fogColumnRevealNearest for the
// OWN-COLUMN DROP only, where the voxel's own world Z is known. They fold the
// per-circle height penalty zCostUp * max(dzUp - freeBand, 0) +
// zCostDown * max(dzDown - freeBand, 0), where dzUp = max(observerZ - voxelZ,
// 0) and dzDown = max(voxelZ - observerZ, 0), into the effective radial
// distance so a boundary voxel clips consistently with FOG_TO_TRIXEL's
// per-pixel z-aware reveal: a pillar top / pit floor far from the observer's
// height drops even though its XY column sits inside the disc. These live HERE
// rather than beside their z-free twins in ir_voxel_face_select.glsl because
// the drop is STAGE-1-ONLY — stage 2 never repeats it — and the shared include
// holds exactly the definitions both stages must agree on. The reveal math is
// INLINED rather than a shared ir_iso_common Z helper, as fogColumnReveal is —
// a new symbol there perturbs the cardinal fast path. The cut-face emission and
// the nearest-cell KEEP widening call the z-free twins in the shared include (a
// column spans all z, so those best-case-z tests keep a superset and never drop
// a voxel a pixel would reveal); only the DROP metric and the nearest-Z
// DISTANCE carry the penalty — the keep-ring WIDTH (kFogHiddenKeepCells) stays
// z-free. All-zero heights make both penalty terms exactly 0, so these return
// exactly the z-free twins' values.
float fogColumnRevealZ(ivec2 col, float voxelZ) {
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
        const vec4 h = visionCircleHeights[i];
        const float dzUp = max(h.x - voxelZ, 0.0);
        const float dzDown = max(voxelZ - h.x, 0.0);
        const float distEff = length(vec2(col) - visionCircles[i].xy) +
            h.y * max(dzUp - h.w, 0.0) + h.z * max(dzDown - h.w, 0.0);
        const float a = max(visionCircles[i].w, 0.0);
        reveal = max(
            reveal, 1.0 - smoothstep(visionCircles[i].z - a, visionCircles[i].z + a, distEff)
        );
    }
    return reveal;
}

float fogColumnRevealNearestZ(ivec2 col, float voxelZ) {
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
        const vec4 h = visionCircleHeights[i];
        const float dzUp = max(h.x - voxelZ, 0.0);
        const float dzDown = max(voxelZ - h.x, 0.0);
        const float distEff = length(nearest - visionCircles[i].xy) +
            h.y * max(dzUp - h.w, 0.0) + h.z * max(dzDown - h.w, 0.0);
        // Keep-ring WIDTH stays z-free (kFogColumnKeepAa + kFogHiddenKeepCells) —
        // a fixed geometric ring so FOG_TO_TRIXEL's cross-section cut always has
        // matter to repaint; only the distance carries the z penalty.
        const float a = max(visionCircles[i].w, kFogColumnKeepAa + kFogHiddenKeepCells);
        reveal = max(
            reveal, 1.0 - smoothstep(visionCircles[i].z - a, visionCircles[i].z + a, distEff)
        );
    }
    return reveal;
}

void main() {
    uint compactedIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * numGroupsX;
    if (compactedIdx >= visibleCount) return;

    // Micro-slice packing: the compact launches ceil(microSliceCount /
    // kStageMicroSlicesPerGroup) z-workgroups, each carrying kStageMicroSlicesPerGroup
    // z-threads. Recover this invocation's flat micro-slice index and discard the
    // tail past microSliceCount. The subdivided path maps zIdx → (u,v); the
    // base + per-axis paths only ever run zIdx 0 (microSliceCount == 1, or an
    // explicit `zIdx != 0` return).
    const int zIdx = int(gl_WorkGroupID.z) * kStageMicroSlicesPerGroup + int(gl_LocalInvocationID.z);
#if IR_FEEDER_PASS
    // The feeder dispatch (struct 1) rasters feederSubCap² micro-cells per face
    // instead of effSub²; its guard must match the compact's writeDispatchDims
    // z-count for this pass exactly.
    const int feederCap = max(feederSubCap, 1);
    const int microSliceCount = (voxelRenderOptions.x != 0) ? (feederCap * feederCap) : 1;
#else
    const int microSliceCount =
        (voxelRenderOptions.x != 0)
            ? (max(voxelRenderOptions.y, 1) * max(voxelRenderOptions.y, 1))
            : 1;
#endif
    if (zIdx >= microSliceCount) return;

#if IR_FEEDER_PASS
    // Feeders were tail-appended by the compact (slot i at feederPassTailBase-1-i);
    // binding 26 is bound to struct 1 for this feeder dispatch, so the
    // `visibleCount`/`numGroupsX` this kernel reads are the feeder count.
    uint voxelIndex = compactedVoxelIndices[uint(feederPassTailBase) - 1u - compactedIdx];
#else
    // The visible list is read forward from struct 0.
    uint voxelIndex = compactedVoxelIndices[compactedIdx];
#endif
    const vec4 voxelPosition = positions[voxelIndex];

    // `slot` is the per-voxel visible-triplet index (0/1/2) — a workgroup
    // label that maps to a diamond region (right column / left column / top
    // row) and to the per-slot deformation matrix. `faceId` is the WORLD
    // FaceId (0..5) the camera sees at this slot, resolved by the CPU per
    // cardinal via `IRMath::visibleFaceTripletCardinal`.
    const int slot = localIDToFace_2x3(gl_LocalInvocationID.xy);
    int faceId = visibleFaceIds[slot];
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);

    // Re-voxelize marker: detached canvases (visibleFaceIds.w != 0) bake the
    // entity rotation into the CELL positions and raster at cardinal 0.
    const bool reVoxelize = visibleFaceIds.w != 0;

    // Exposed-face gate: emit only when the world face this slot renders is
    // BOTH camera-visible (guaranteed by the slot-to-faceId resolution) AND
    // exposed (neighbor cell empty). Interior voxels of a solid cube emit
    // nothing — surface area, not volume — so an interior +X copy can never
    // depth-tie an exterior -X copy into a stripe artifact. Bit position
    // matches `IRComponents::VoxelFlags::kFaceOccluded(faceId)`.
    //
    // Re-voxelize canvases gate here too: the GPU scatter (c_revoxelize_detached
    // MODE 1) authors the ROTATED-frame exposed mask from dest-grid adjacency
    // (the GPU twin of REBUILD_GRID_VOXELS' CPU mask), so `flags_` is valid in
    // the rotated frame. `reVoxelize` also drives the emit dilation.
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;

    // Face selection — the visible-triplet × exposed-mask gate, the
    // silhouette-riser flip + dual-emit predicate for rotated content, and the
    // fog cut-face widening — is shared with stage 2 via
    // ir_voxel_face_select.glsl: both stages key their taps off ONE definition,
    // so the colour tap cannot desync from the distance tap.
    const VoxelFaceSelect sel = selectVoxelFace(
        faceId, reVoxelize, voxels[voxelIndex].reserved, flagsByte,
        voxelPosition, perAxisRoute, isDetachedCanvas, detachedWorldReceive
    );
    if (!sel.keepFace) return;
    faceId = sel.faceId;
    const int riserFlip = sel.riserFlip;
    const bool bothPolaritiesExposed = sel.bothPolaritiesExposed;
    const bool fogWholeBodyExempt = (voxels[voxelIndex].reserved & (1u << 3u)) != 0u;

    // Per-voxel analytic fog clip — STAGE-1-ONLY (stage 2 never repeats it:
    // with the distances dropped, its colour taps are rejected by the depth
    // re-test). On the single-canvas world fog route, drop a voxel whose OWN
    // world column is FULLY hidden (reveal <= 0) so FOG_TO_TRIXEL can't
    // hard-black its faces; a PARTIALLY revealed boundary column is KEPT so
    // FOG_TO_TRIXEL fades the object's silhouette on the same smooth curve as
    // the floor. The GRID canvas keeps a disc-adjacent RING of hidden columns
    // (fogColumnRevealNearest's kFogHiddenKeepCells) so the image-space fog cut
    // has hidden matter to repaint; a world-placed DETACHED canvas carries no
    // fog pass, so it clips tight at the voxel lattice (fogColumnReveal <= 0).
    // The per-axis rotation routes run the SAME reveal<=0 clip inside the
    // per-axis branch; the explicit perAxisRoute==0 term keeps this a no-op on
    // routes 1/2/3, and non-fog scenes short-circuit on fogActive.
    // The drop uses this voxel's OWN world Z so a height-penalized voxel
    // (pillar top / pit floor far from the observer height) clips consistently
    // with FOG_TO_TRIXEL's per-pixel z reveal. Only the DROP takes the Z twins —
    // the cut-face test inside selectVoxelFace stays on the z-free
    // fogColumnReveal (a column spans all z, so that best-case-z test keeps a
    // superset and never drops a voxel a pixel would reveal). All-zero heights
    // reduce this to the 2D drop.
    bool ownColumnHidden = isDetachedCanvas > 0.5
        ? fogColumnRevealZ(sel.worldColumn, voxelPosition.z) <= 0.0
        : fogColumnRevealNearestZ(sel.worldColumn, voxelPosition.z) <= 0.0;
    if (!fogWholeBodyExempt && sel.fogActive && perAxisRoute == 0 && ownColumnHidden) {
        return;
    }

    // At cardinalIndex==0 the rotation is the identity; gating it behind a
    // branch keeps the GLSL/MSL compilers from reshuffling instructions or
    // changing depth-tie ordering on the GPU, so the yaw-0 raster is exactly
    // the unrotated raster.

    // Per-slot deformation matrix — `D` shapes the diamond corner offsets
    // under residualYaw and (for detached canvas) per-face SO(3). At cardinal
    // 0 + residualYaw==0 every slot's D is the identity, so the per-slot
    // path collapses to faceOffset_2x3(slot, subPixel).
    const mat2 D = mat2(faceDeform[slot].xy, faceDeform[slot].zw);

    // Smooth camera Z-yaw per-axis routing
    // (docs/design/per-axis-trixel-canvas-rotation.md). At perAxisRoute 1/2/3 this
    // rasterizes the X/Y/Z axis canvas: emit ONLY the visible face on that
    // axis and store ONE cell per face (not the emitDeformedFace super-sampled
    // cluster). `atomicMin` resolves voxel-vs-voxel occlusion per cell (nearest
    // face on this view ray wins), so every non-empty cell is exactly one
    // occlusion-winning face. The framebuffer scatter
    // (system_trixel_to_framebuffer) forward-projects each non-empty cell as
    // its true deformed face quad — recovering the world origin from
    // (cell - perAxisBase, decodeDepthPerAxis, visualYaw) — with no
    // gather/parity inverse, so the gather-parity stripe class cannot occur.
    // The face SHAPE is reconstructed at scatter time, so the per-slot deform D
    // is not applied here.
    if (perAxisRoute != 0) {
        // Per-axis own-column fog clip: the same drop as the single-canvas route
        // (reveal <= 0 — FULLY hidden), applied on EVERY axis route (1/2/3) so a
        // rotating boundary object clips its hidden half identically (a hidden
        // column's Z face would otherwise float on route 3). visionCircleCount==0
        // and the 1×1 placeholder grid short-circuit non-fog rotating scenes.
        // The two arguments round differently on purpose: the COLUMN is rounded
        // because it indexes the integer fog grid, while the HEIGHT stays the raw
        // continuous voxelPosition.z. Rounding the height would quantize the
        // penalty into whole world-Z steps AND disagree with c_fog_to_trixel's
        // per-pixel reveal, which penalizes against the unrounded `pos3D.z`.
        // The single-canvas route splits the same way (sel.worldColumn is
        // rounded; its z argument is not).
        if (!fogWholeBodyExempt && visionCircleCount > 0 &&
            fogColumnRevealZ(roundHalfUp(voxelPosition.xyz).xy, voxelPosition.z) <= 0.0) {
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
        // occlusion (resolved by the rawDepth atomicMin). The index is UN-yawed:
        // a yawed iso store would collapse the compressed axis and make the
        // inverse singular, whereas here the recovery `isoPixelToPos3D` is exact
        // at every yaw. The scatter reprojects the recovered origin under the
        // live yaw.
        // Whole-iso base anchor: the per-axis store is BASE-resolution, so the
        // anchor must NOT be density-scaled like the subdivided cardinal canvas
        // (a density-scaled anchor jitters under pan). The cardinal single-canvas
        // paths keep trixelFrameOffset (their content IS subdivided).
        const ivec2 perAxisBase = trixelOriginOffsetZ1(canvasSizePixels) + ivec2(floor(frameCanvasOffset));
        // Store at BASE (world-unit) resolution regardless of effSub — on the
        // subdivided path only the z=0 invocation writes (the voxel's
        // continuous sub-cell offset rides the encoding so the scatter can
        // sub-pixel-shift the face quad).
        if (voxelRenderOptions.x != 0 && zIdx != 0) return;
        int voxelDistance;
        const ivec3 facePos =
            perAxisStoreFacePos(voxelPosition, faceId, slot, axis, riserFlip, voxelDistance);
        if (resolveMode == 3) {
            overflowAppendTap(perAxisBase, facePos, voxelDistance, voxels[voxelIndex].colorPacked);
            return;
        }
        // Equal keys arise from perAxisStoreFacePos's 4-bit frac quantization —
        // the winner election here is what keeps the stage-2 color tap
        // deterministic among them.
        if (resolveMode != 0) {
            resolveWinnerTap(perAxisBase + pos3DtoPos2DIso(facePos), voxelDistance, voxelIndex);
            return;
        }
        writeDistanceTap(perAxisBase + pos3DtoPos2DIso(facePos), voxelDistance);
        viewMaskTap(perAxisBase, facePos);
        return;
    }

    if (voxelRenderOptions.x == 0) {
        // roundHalfUp, not hardware round(): half-integer voxel positions must
        // resolve to the same cell here, in stage 2's re-derivation, and in the
        // CPU-side IRMath::roundHalfUp consumers — hardware round() ties are
        // implementation-defined and leave a one-cell seam along tie planes.
        ivec3 voxelPositionInt = roundHalfUp(voxelPosition.xyz);
        if (cardinalIndex != 0) {
            // Plain cardinal rotation — no lower-corner shift: it renders the
            // cardinal-0 raster of the rotated scene, anchored on the authored
            // position like the SDF path and the picking inverses. A
            // lower-corner shift would rotate the mass about its lower-corner
            // lattice (anchor p + 0.5), orbiting any pinned focus.
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
        }
        // Encode `slot` (not faceId) in depth — it fits the 2-bit field, and
        // AO/lighting recover the world faceId via visibleFaceIds[slot] from
        // the same UBO.
        // Detached entities raster in model space (camera yaw zeroed), so the
        // occlusion order projects onto the entity-rotated iso axis.
        // The world canvas keeps the fixed (1,1,1) via pos3DtoDistance
        // (voxelPositionInt is integer, so the detached path's
        // roundHalfUp(dot(pos,(1,1,1))) would equal x+y+z at identity too —
        // the branch only guards the GRID fast path).
        const int rawDepth = isDetachedCanvas > 0.5
            ? isoDepthAlongAxis(voxelPositionInt, voxelDepthAxis.xyz)
            : pos3DtoDistance(voxelPositionInt);
        const int voxelDistance = encodeDepthWithFace(rawDepth, slot, riserFlip);
        const ivec2 base =
            trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions) +
            pos3DtoPos2DIso(voxelPositionInt);
        emitDeformedFace(
            base, D, voxelDistance, faceId, reVoxelize
#if IR_STORE_WINNER_ELECTION
            , voxelIndex
#endif
        );
        return;
    }

    const int subdivisions = max(voxelRenderOptions.y, 1);
#if IR_FEEDER_PASS
    // Strided feeder micro-grid — a coarser STRIDED SUBSET of the full
    // [0,subdivisions)² face cells (NOT a corner block, so a bake sample lands
    // across the whole face). Integer (i*subdivisions)/cap is monotone +
    // full-span; cap == subdivisions degenerates to the visible identity
    // mapping. Geometry stays in `subdivisions` units — only the sampling
    // density drops.
    const int u = ((zIdx / feederCap) * subdivisions) / feederCap;
    const int v = ((zIdx % feederCap) * subdivisions) / feederCap;
#else
    const int u = zIdx / subdivisions;
    const int v = zIdx % subdivisions;
#endif

    const vec3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const ivec3 voxelPositionFixed = roundHalfUp(voxelPositionAligned * float(subdivisions));
    const ivec2 frameOffsetFixed =
        trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions);

    // Six-face micro position — POS faces start at the cell's high-coordinate
    // side (`+ subdivisions` on the fixed axis), NEG faces at the low side. At
    // a non-zero cardinal the micro position is computed NATIVELY IN VIEW
    // SPACE — rotate the CELL origin (scale is per-world-unit, matching
    // `voxelPositionFixed = round(worldPos * subdivisions)`), rotate the FACE
    // ID, then run the same cardinal-0 face math on the pair. Rotating a
    // world-computed face plane after the fact instead treats a plane BOUNDARY
    // as a cell index: a rotated-in POS face lands one sub-unit past its
    // neighbor faces' coverage, leaving a background seam along every shared
    // edge of that face at cardinals 1/2/3. View-space math is seam-free by
    // construction (it IS the cardinal-0 raster of the rotated scene) and
    // matches the exact inverse recovery (`trixelCanvasPixelToWorld3D`).
    // Stage 2 mirrors this exactly for the colour tap.
    ivec3 viewCellFixed = voxelPositionFixed;
    int viewFaceId = faceId;
    if (cardinalIndex != 0) {
        // Plain cardinal rotation — no lower-corner shift, as on the
        // unsubdivided path. The whole cell grid translates uniformly, so face
        // adjacency (and the seam-free view-space face math) is preserved;
        // stage 2 mirrors this exactly.
        viewCellFixed = rotateCardinalZ(voxelPositionFixed, cardinalIndex);
        viewFaceId = rotateFaceIdCardinalZ(faceId, cardinalIndex);
    }
    const ivec3 microPositionFixed =
        faceMicroPositionFixed6(viewFaceId, viewCellFixed, u, v, subdivisions);
    // Detached entities project occlusion depth onto the entity-rotated iso
    // axis; world/GRID keeps the (x+y+z) fixed-(1,1,1) form. Depth is
    // in subdivision units on both branches, so the encode scale is unchanged.
    const int depthBase = isDetachedCanvas > 0.5
        ? isoDepthAlongAxis(microPositionFixed, voxelDepthAxis.xyz)
        : (microPositionFixed.x + microPositionFixed.y + microPositionFixed.z);
    const int voxelDistance = encodeDepthWithFace(depthBase, slot, riserFlip);
    const ivec2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(
        base, D, voxelDistance, viewFaceId, reVoxelize
#if IR_STORE_WINNER_ELECTION
        , voxelIndex
#endif
    );

    // Both-exposed dual emit: the opposite face plane rasters its own
    // pixels here (faceMicroPositionFixed6 is polarity-dependent), so the riser
    // needs its own deformed-face emit — same view-space form as the primary.
    // `viewFaceId ^ 1` after rotation == rotating the opposite face, since
    // rotateFaceIdCardinalZ maps opposite-face pairs to opposite-face pairs.
    if (bothPolaritiesExposed) {
        const ivec3 microOpposite =
            faceMicroPositionFixed6(viewFaceId ^ 1, viewCellFixed, u, v, subdivisions);
        const int depthOpposite = isDetachedCanvas > 0.5
            ? isoDepthAlongAxis(microOpposite, voxelDepthAxis.xyz)
            : (microOpposite.x + microOpposite.y + microOpposite.z);
        // The opposite plane is by construction the non-triplet polarity of
        // this slot (riserFlip is always 0 when both polarities are exposed).
        const int distanceOpposite = encodeDepthWithFace(depthOpposite, slot, riserFlip ^ 1);
        const ivec2 baseOpposite = frameOffsetFixed + pos3DtoPos2DIso(microOpposite);
        emitDeformedFace(
            baseOpposite, D, distanceOpposite, viewFaceId ^ 1, reVoxelize
#if IR_STORE_WINNER_ELECTION
            , voxelIndex
#endif
        );
    }
}
