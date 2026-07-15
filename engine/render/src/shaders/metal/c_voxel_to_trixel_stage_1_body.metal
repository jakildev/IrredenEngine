// Shared stage-1 compute BODY (#2258 Step B, architect option a′) — Metal twin
// of c_voxel_to_trixel_stage_1_body.glsl. An include-FRAGMENT, not a standalone
// shader: the two thin wrappers supply the prerequisite includes, the
// `#define IR_FEEDER_PASS {0|1}`, and the `#define IR_STAGE1_KERNEL_NAME`, then
// `#include` this body. The wrappers:
//   c_voxel_to_trixel_stage_1.metal        → IR_FEEDER_PASS 0, kernel name
//     c_voxel_to_trixel_stage_1        (visible dispatch)
//   c_voxel_to_trixel_stage_1_feeder.metal → IR_FEEDER_PASS 1, kernel name
//     c_voxel_to_trixel_stage_1_feeder (shadow-feeder dispatch)
// The feeder-only code (tail read + strided micro-grid) is fenced under
// `#if IR_FEEDER_PASS`, so the visible kernel compiles with the feeder branches
// textually absent — no runtime predication tax on the hottest kernel. Metal's
// loadAndPreprocessMetalSource IS recursive, but the body is kept include-free
// to mirror the (non-recursive) GLSL idiom.

// Stage 1 of the voxel→trixel pipeline: each surviving voxel writes a depth
// tap into the canvas distance scratch buffer using atomic-min, so stage 2
// can do front-face resolution.  Reads compacted visible voxel indices
// produced by c_voxel_visibility_compact.metal.
//
// MSL has no portable image-atomic syntax across all macOS versions, so we
// use a sibling scratch buffer the same size as the R32I distance texture.
// See engine/render/include/irreden/render/metal/metal_runtime.hpp.

struct IndirectDispatchParamsRO {
    uint numGroupsX;
    uint numGroupsY;
    uint numGroupsZ;
    uint visibleCount;
};

inline void writeDistanceTap(
    int2 canvasPixel,
    int voxelDistance,
    device atomic_int* distanceScratch,
    int2 canvasSize
) {
    if (!isInsideCanvas(canvasPixel, canvasSize)) {
        return;
    }
    const uint linearIndex =
        uint(canvasPixel.y) * uint(canvasSize.x) + uint(canvasPixel.x);
    atomic_fetch_min_explicit(
        &distanceScratch[linearIndex],
        voxelDistance,
        memory_order_relaxed
    );
}

// Winner-resolve tap (#2255, resolveMode == 1) — GLSL twin in
// c_voxel_to_trixel_stage_1.glsl. Among the faces whose encoded distance
// equals the settled atomic-min winner at this cell, elect the smallest
// run-stable voxel pool index into the winner scratch (a manually-managed
// atomic buffer at kBufferIndex_PerAxisResolveScratch — Metal's single
// image-atomic scratch slot is held by the distance store, so the winner
// cannot be a second scratch image). Stage 2's matching guard then admits
// exactly one writer per cell.
inline void resolveWinnerTap(
    int2 canvasPixel,
    int voxelDistance,
    uint voxelIndex,
    device const atomic_int* distanceScratch,
    device atomic_uint* perAxisWinnerIds,
    int2 canvasSize
) {
    if (!isInsideCanvas(canvasPixel, canvasSize)) {
        return;
    }
    const uint linearIndex =
        uint(canvasPixel.y) * uint(canvasSize.x) + uint(canvasPixel.x);
    if (atomic_load_explicit(&distanceScratch[linearIndex], memory_order_relaxed) !=
        voxelDistance) {
        return;
    }
    atomic_fetch_min_explicit(&perAxisWinnerIds[linearIndex], voxelIndex, memory_order_relaxed);
}

// View-visibility overflow lane (#2333) — GLSL twin in
// c_voxel_to_trixel_stage_1_body.glsl. Yawed-depth quantization shared by
// resolveMode 2 (mask write) and 3 (mask compare): 1/16-world-unit steps,
// biased to a uint so atomic-min orders negative depths correctly. Both modes
// call THE SAME function on the SAME facePos, so a face always ties its own
// mask entry exactly regardless of float rounding.
constant float kOverflowDepthQuantScale = 16.0f;
// Half a world unit of tolerance (8 sixteenth-steps): absorbs quantization
// ties between genuinely co-visible faces without admitting occluded coset
// losers (the nearest coset pair separates by >= ~2.7 world units of yawed
// depth). Over-emit is safe — the framebuffer depth test cleans up; under-emit
// re-opens the #2331 holes.
constant uint kOverflowDepthEpsSteps = 8u;
constant int kOverflowDepthBias = 0x40000000;

inline uint overflowYawedDepthKey(int3 facePos, float visualYaw) {
    return uint(
        int(floor(yawedIsoDistance(float3(facePos), visualYaw) * kOverflowDepthQuantScale)) +
        kOverflowDepthBias
    );
}

// The face's screen cell at the LIVE yaw, on the same perAxisBase anchor the
// cardinal store uses (the scatter projects with the identical
// pos3DtoPos2DIsoYawed, so mask cells and scattered quads agree).
inline int2 overflowYawedPixel(int2 perAxisBase, int3 facePos, float visualYaw) {
    return perAxisBase + roundHalfUp(pos3DtoPos2DIsoYawed(float3(facePos), visualYaw));
}

// resolveMode == 2: view-mask write. Every per-axis face (all three axis
// routes — view visibility competes across axes) atomic-mins its quantized
// yawed depth into the shared mask region of the buffer-28 scratch.
inline void viewMaskTap(
    int2 perAxisBase,
    int3 facePos,
    constant FrameDataVoxelToTrixel& frameData,
    device atomic_uint* scratch
) {
    const int2 yawedPix = overflowYawedPixel(perAxisBase, facePos, frameData.visualYaw);
    if (!isInsideCanvas(yawedPix, frameData.canvasSizePixels)) {
        return;
    }
    const uint cell =
        uint(yawedPix.y) * uint(frameData.canvasSizePixels.x) + uint(yawedPix.x);
    atomic_fetch_min_explicit(
        &scratch[uint(frameData.overflowScratchLayout.x) + cell],
        overflowYawedDepthKey(facePos, frameData.visualYaw),
        memory_order_relaxed
    );
}

// resolveMode == 3: overflow append. A face appends iff it is view-visible
// (within epsilon of its view-mask cell winner) AND it is NOT its cardinal
// store cell's settled winner — exactly the set `viewVisible \ cardinalWinners`
// the cardinal-keyed store drops (docs: epic #2331). Entries carry the exact
// (cardinal cell, encoded distance) pair the store would have written plus the
// raw colorPacked, so the scatter's overflow branch reuses the per-cell
// recovery bit-for-bit (albedo-only in this child; lighting is #2334). Entry
// words are written with relaxed atomic stores — the scratch is declared
// atomic_uint, and a plain-store reinterpret would be UB.
inline void overflowAppendTap(
    int2 perAxisBase,
    int3 facePos,
    int voxelDistance,
    uint colorPacked,
    constant FrameDataVoxelToTrixel& frameData,
    device const atomic_int* distanceScratch,
    device atomic_uint* scratch,
    int2 canvasSize
) {
    const int2 yawedPix = overflowYawedPixel(perAxisBase, facePos, frameData.visualYaw);
    if (!isInsideCanvas(yawedPix, frameData.canvasSizePixels)) {
        return; // off-screen at the live yaw
    }
    const uint yawedCell =
        uint(yawedPix.y) * uint(frameData.canvasSizePixels.x) + uint(yawedPix.x);
    const uint maskKey = atomic_load_explicit(
        &scratch[uint(frameData.overflowScratchLayout.x) + yawedCell], memory_order_relaxed
    );
    if (overflowYawedDepthKey(facePos, frameData.visualYaw) >
        maskKey + kOverflowDepthEpsSteps) {
        return; // view-occluded — some nearer face owns this screen cell
    }
    const int2 cardPix = perAxisBase + pos3DtoPos2DIso(facePos);
    // Off-canvas cardinal key never stored (writeDistanceTap dropped it) and is
    // outside the worst-case-sized render domain — mirror the silent drop.
    if (!isInsideCanvas(cardPix, canvasSize)) {
        return;
    }
    const uint cardCell = uint(cardPix.y) * uint(canvasSize.x) + uint(cardPix.x);
    if (atomic_load_explicit(&distanceScratch[cardCell], memory_order_relaxed) ==
        voxelDistance) {
        return; // this face IS (or ties) the settled winner — the cell path draws it
    }
    const uint ctrlBase = uint(frameData.overflowScratchLayout.y);
    const uint idx =
        atomic_fetch_add_explicit(&scratch[ctrlBase + 1u], 1u, memory_order_relaxed);
    if (idx >= uint(frameData.overflowScratchLayout.w)) {
        // Cap hit: pair the add back off so instanceCount settles at exactly
        // min(appends, cap), and count the drop for the CPU one-shot warn
        // (never silent). No reader sees the transient over-cap value — the
        // indirect draw is barriered behind this whole dispatch.
        atomic_fetch_sub_explicit(&scratch[ctrlBase + 1u], 1u, memory_order_relaxed);
        atomic_fetch_add_explicit(&scratch[ctrlBase + 5u], 1u, memory_order_relaxed);
        return;
    }
    const uint entryBase = uint(frameData.overflowScratchLayout.z) + idx * 3u;
    atomic_store_explicit(
        &scratch[entryBase + 0u],
        (uint(cardPix.x) & 0xFFFFu) | ((uint(cardPix.y) & 0xFFFFu) << 16u),
        memory_order_relaxed
    );
    atomic_store_explicit(&scratch[entryBase + 1u], colorPacked, memory_order_relaxed);
    atomic_store_explicit(&scratch[entryBase + 2u], uint(voxelDistance), memory_order_relaxed);
}

// Emit a face's 2x3 trixel block through the deformation matrix D.
// World canvas: maxN=2 (Z-yaw residual ≤ π/4, column lengths ≤ √3).
// Detached canvas: maxN=6 (full SO(3)).
// See c_voxel_to_trixel_stage_1.glsl for the full contract.
// KEEP IN SYNC with c_voxel_visibility_compact.{glsl,metal} voxelOccludedByHiZ:
// the per-voxel Hi-Z occlusion cull's sampled window MUST be a conservative
// superset of this function's write set (`base + roundHalfUp(D * src)` over the
// [0,2)x[0,3) invocation lattice) so a visible voxel's own last-frame write always
// lands in the window it is tested against. Widening this emission hull without
// widening that window re-introduces the #1812 static-scene silhouette holes.
inline void emitDeformedFace(
    int2 base,
    float2x2 D,
    int voxelDistance,
    uint2 localId,
    bool isDetached,
    int faceId,
    bool reVoxelize,
    device atomic_int* distanceScratch,
    int2 canvasSize
) {
    const int maxN = isDetached ? 6 : 2;
    const int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    const float inv = 1.0 / float(n);
    // Conservative coverage (#1557 Option B) — mirror of the GLSL twin. A
    // re-voxelize canvas bakes the rotation into integer CELL positions, so
    // round-to-cell leaves sub-cell gaps; dilate each surface face ±1px along its
    // in-plane iso axes so the gaps fill with the nearest face (atomicMin keeps
    // the occlusion winner; stage 2's depth re-test paints the matching colour).
    int2 su = int2(0);
    int2 sv = int2(0);
    if (reVoxelize) {
        faceInPlaneIsoSteps(faceId, su, sv);
    }
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            const float2 src = float2(localId) + float2(float(sx), float(sy)) * inv;
            const int2 p = base + roundHalfUp(D * src);
            writeDistanceTap(p, voxelDistance, distanceScratch, canvasSize);
            if (reVoxelize) {
                writeDistanceTap(p + su, voxelDistance, distanceScratch, canvasSize);
                writeDistanceTap(p - su, voxelDistance, distanceScratch, canvasSize);
                writeDistanceTap(p + sv, voxelDistance, distanceScratch, canvasSize);
                writeDistanceTap(p - sv, voxelDistance, distanceScratch, canvasSize);
            }
        }
    }
}

// 12 B per voxel — must match C_Voxel layout in
// engine/prefabs/irreden/voxel/components/component_voxel.hpp.
struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};

// Face-occlusion bit indices live at `2 + faceId` in `materialFlagBone`'s
// byte 5, mirroring `IRComponents::VoxelFlags::kFaceOccluded*`. The
// exposed-face test (visible-triplet × exposed-mask, #1278) is centralized
// in `faceIsExposed(flagsByte, faceId)` from ir_iso_common.metal.

// Per-voxel analytic fog clip inputs (#2102), mirroring c_voxel_visibility_compact
// + c_fog_to_trixel. The world fog canvas binds its 256² grid texture at
// [[texture(0)]] and the live vision circles at [[buffer(27)]]; every non-fog /
// detached canvas binds a 1×1 all-visible placeholder + a count-0 observer
// buffer, so fogColumnReveal short-circuits to "fully visible" and those
// scenes stay byte-identical.
constant int kFogOfWarHalfExtent = 128;
constant float kFogExploredThreshold = 0.25f;
constant int kMaxFogVisionCircles = 8; // mirror of component_canvas_fog_of_war.hpp kMaxFogVisionCircles
struct FogObserverData {
    float4 visionCircles[kMaxFogVisionCircles]; // (centerX, centerY, radius, edgeSoftness)
    int visionCircleCount;
    int _fogObsPad0;
    int _fogObsPad1;
    int _fogObsPad2;
};

// Fog reveal of world grid COLUMN `col` (xy world units) in [0,1]: the strongest
// live vision-disc reveal at the column center (aa = 0), or 1.0 for explored grid
// memory / OOB / the 1×1 placeholder. See the GLSL twin
// (c_voxel_to_trixel_stage_1.glsl::fogColumnReveal) for the full rationale: the
// compact keeps a permissive margin for the smooth per-pixel floor edge, and
// STAGE_1 tightens it for the voxel object. Two callers threshold this — the #2102
// own-column drop (drop reveal <= 0, FULLY hidden; a partially-revealed boundary
// column rasterizes so FOG_TO_TRIXEL fades it like the floor, #2126 Mode B) and
// the #2125/#2126 neighbor cut-face test (cut reveal < 1.0, "not fully revealed").
// For a hard disc reveal is binary {0,1} so all thresholds collapse to "outside
// radius" → Mode A + non-fog byte-identical. Duplicated byte-identically in
// c_voxel_to_trixel_stage_2.metal (fog grid on [[texture(3)]] there); keep in sync.
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

// Own-column DROP reveal, evaluated at the cell point NEAREST each vision-circle
// center rather than the cell center (#2124 screen-space cross-section). The drop
// keeps a column iff this is > 0, so a column the disc merely CLIPS (center
// outside R, but the unit cell still overlaps the reveal region) is KEPT and
// rasters its full footprint — FOG_TO_TRIXEL then trims it per pixel at the exact
// analytic edge (a game-resolution silhouette) instead of the geometry ending on
// the voxel lattice (the #2102 voxel-jagged edge). Evaluating at the nearest cell
// point keeps ONLY the one-cell ring the disc crosses (tight, not a blanket
// margin); kFogColumnKeepAa is a small rim so the hard-disc smoothstep is
// non-degenerate and the AA rim / reconstruction skew never notches the edge.
// Grid-memory / OOB / placeholder short-circuits match fogColumnReveal.
constant float kFogColumnCellHalf = 0.5f;
constant float kFogColumnKeepAa = 0.5f;
// kFogHiddenKeepCells widens the keep into a RING of fog-hidden columns
// around the disc (#2124 analytic cross-section): FOG_TO_TRIXEL's image-space
// cut repaints a hidden pixel as the cylinder's cut surface, so hidden matter
// near the rim must still RENDER for the cut to have colour to work from.
// See the GLSL twin for the full rationale; mirrored in the compact's
// kCullSafetyCells (keep superset).
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

kernel void IR_STAGE1_KERNEL_NAME(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    device const float4* positions [[buffer(5)]],
    device const Voxel* voxels [[buffer(6)]],
    device const uint* compactedVoxelIndices [[buffer(25)]],
    device const IndirectDispatchParamsRO& indirectParams [[buffer(26)]],
    device atomic_int* distanceScratch [[buffer(16)]],
    texture2d<float, access::read> canvasFogOfWar [[texture(0)]],
    constant FogObserverData& fogObservers [[buffer(27)]],
    device atomic_uint* perAxisWinnerIds [[buffer(28)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId3 [[thread_position_in_threadgroup]]
) {
    const uint compactedIdx = groupId.x + groupId.y * indirectParams.numGroupsX;
    if (compactedIdx >= indirectParams.visibleCount) {
        return;
    }

    // #2258 micro-slice packing — mirrors the GLSL twin. The threadgroup z-size
    // is kStageMicroSlicesPerGroup (metal_pipeline.cpp map); recover this
    // invocation's flat micro-slice index and discard the tail past
    // microSliceCount. Byte-identical to the pre-#2258 one-z-group-per-slice
    // dispatch. See c_voxel_to_trixel_stage_1.glsl.
    const int zIdx =
        int(groupId.z) * kStageMicroSlicesPerGroup + int(localId3.z);
#if IR_FEEDER_PASS
    // #2258 Step B: the feeder dispatch (struct 1) rasters feederSubCap²
    // micro-cells per face instead of effSub²; the guard must match the
    // compact's writeDispatchDims z-count for this pass. feederCap also scopes
    // the strided (u,v) derivation, both under this IR_FEEDER_PASS fence. See
    // the GLSL twin.
    const int feederCap = max(frameData.feederSubCap, 1);
    const int microSliceCount =
        (frameData.voxelRenderOptions.x != 0) ? (feederCap * feederCap) : 1;
#else
    const int microSliceCount = (frameData.voxelRenderOptions.x != 0)
        ? (max(frameData.voxelRenderOptions.y, 1) * max(frameData.voxelRenderOptions.y, 1))
        : 1;
#endif
    if (zIdx >= microSliceCount) {
        return;
    }

#if IR_FEEDER_PASS
    // Feeders were tail-appended by the compact (slot i at feederPassTailBase-1-i);
    // binding 26 is bound to struct 1 for this feeder dispatch, so the
    // numGroupsX/visibleCount this kernel reads are the feeder struct's.
    const uint voxelIndex =
        compactedVoxelIndices[uint(frameData.feederPassTailBase) - 1u - compactedIdx];
#else
    // The visible list is read forward from struct 0.
    const uint voxelIndex = compactedVoxelIndices[compactedIdx];
#endif
    const float4 voxelPosition = positions[voxelIndex];
    const uint2 localId = localId3.xy;
    // See c_voxel_to_trixel_stage_1.glsl for the slot/faceId contract (#1278).
    const int slot = localIDToFace_2x3(localId);
    int faceId = frameData.visibleFaceIds[slot];
    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);

    const int2 canvasSize = frameData.canvasSizePixels;

    // Re-voxelize marker (frameData.visibleFaceIds.w != 0, #1557) — see GLSL.
    const bool reVoxelize = frameData.visibleFaceIds.w != 0;

    // Exposed-face gate (#1278), BYPASSED for re-voxelize (#1570). The GPU
    // scatter (c_revoxelize_detached) rewrites only cell POSITIONS; the per-voxel
    // exposed mask in `flags_` is computed once at authoring time in the
    // UNROTATED model frame and is never recomputed against the rotated cells
    // (P1's per-frame CPU recompute was removed in P2 / #1556), so gating
    // rotated-frame faces against it drops whole camera-visible faces as the
    // solid spins. Re-voxelize emits all three visible-triplet cardinal faces and
    // lets the depth re-test keep the front-most surface — see the GLSL twin for
    // the full rationale.
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;

    // Silhouette-riser face selection (rotated-footprint gap fix) — see the GLSL
    // twin for the full rationale. A rotated voxel staircase's camera-side grazing
    // edge presents the OPPOSITE polarity of an axis from the convex-cardinal
    // triplet; emit it when the triplet face is occluded but the opposite is
    // exposed. GATED to rotated content (detached re-voxelize uniform OR the
    // per-voxel kRotatedEmit marker, reserved bit 2) so static / axis-aligned
    // content keeps the strict triplet and stays byte-identical. Stage 2 mirrors
    // this flip + gate exactly.
    const bool rotatedEmit = reVoxelize || (voxels[voxelIndex].reserved & 4u) != 0u;
    // Polarity carrier (#2207) — riserFlip marks every emit below as the
    // opposite polarity of this slot's triplet face. See the GLSL twin.
    int riserFlip = 0;
    if (rotatedEmit && !faceIsExposed(flagsByte, faceId) &&
        faceIsExposed(flagsByte, faceId ^ 1)) {
        faceId = faceId ^ 1;
        riserFlip = 1;
    }
    // Both-exposed silhouette-riser dual emit (#2157) — see the GLSL twin for
    // the full rationale. Stage 2 mirrors this predicate + the emit site.
    const bool bothPolaritiesExposed = rotatedEmit && faceIsExposed(flagsByte, faceId) &&
        faceIsExposed(flagsByte, faceId ^ 1);
    // Re-voxelize now authors the ROTATED-frame exposed mask on the GPU
    // (c_revoxelize_detached) like the GRID path's #1720, so it gates on
    // faceIsExposed too (no all-3-face bypass → no slot-tie AO hatching). The
    // reVoxelize flag still drives the ±1px dilation in emitDeformedFace.
    //
    // World fog route + world-column recovery (#2125 GRID, #2127 detached; per-axis
    // #2128) — see the GLSL twin. `fogActive` is the single cheap (uniform-only)
    // gate that keeps every non-fog / plain-DETACHED / Z-route voxel byte-identical
    // to master AND free of the world-column rounding below. It fires on the world
    // fog route (perAxisRoute==0, GRID or world-placed re-voxelize detached, #2127)
    // AND the X/Y per-axis rotation routes (1/2, #2128). The GRID world canvas + the
    // X/Y per-axis canvases raster in world space (offset 0); a world-placed detached
    // re-voxelize canvas rasters in the pool-centered MODEL frame, so recover its
    // world column as model + the published cell origin (the SAME model+offset
    // recovery c_lighting_to_trixel uses; the re-voxelize bake is a pure translation
    // off world, so the model-frame face normal equals the world-frame one).
    const bool fogActive = fogObservers.visionCircleCount > 0 &&
        frameData.perAxisRoute <= 2 &&
        (frameData.isDetachedCanvas < 0.5f ||
         (frameData.perAxisRoute == 0 && frameData.detachedWorldReceive.w != 0.0f));
    int2 worldColumn = int2(0);
    if (fogActive) {
        worldColumn = roundHalfUp(voxelPosition.xyz).xy +
            (frameData.isDetachedCanvas > 0.5f
                 ? roundHalfUp(frameData.detachedWorldReceive.xy)
                 : int2(0));
    }

    // Exposed-face gate widened with the fog CUT-FACE rule (#2125/#2127; per-axis
    // #2128) — see the GLSL twin. A non-exposed VERTICAL face (faceId 0..3 = ±X/±Y)
    // becomes the object's interior cross-section wall when its solid neighbor world
    // COLUMN is NOT fully revealed; world fog route (perAxisRoute==0, GRID or
    // world-placed re-voxelize detached via `fogActive`) plus the per-axis rotation
    // routes. `fogActive` carries the per-axis route selection (routes 0/1/2): under
    // yaw the cut face rides the matching axis canvas via the `(faceId>>1)!=axis`
    // store filter below; the Z route (3) carries no cut faces. Keeping a
    // `perAxisRoute` comparison term in `fogActive` makes the Metal compiler schedule
    // the non-fog per-axis store the SAME as the pre-#2128 `== 0` gate → byte-
    // identical. Keep byte-identical to stage 2's gate so distance + colour agree.
    // P2 (#2126) cuts when the neighbor is "not fully revealed" (reveal < 1.0) so the
    // cut wall fills the whole soft band; a hard disc collapses it to the binary
    // boundary (Mode A byte-identical, so #2127's re-voxelize detached cut keeps its
    // merged behaviour at the default).
    bool keepFace = faceIsExposed(flagsByte, faceId);
    if (!keepFace && faceId < kFaceZNeg && fogActive) {
        keepFace = fogColumnReveal(
            canvasFogOfWar, fogObservers,
            worldColumn + faceOutwardNormal6I(faceId).xy) < 1.0f;
    }
    if (!keepFace) return;

    // Per-voxel analytic fog clip (#2102 + #2126 P2 + #2127; per-axis split #2128) —
    // see the GLSL twin. On the single-canvas world route (perAxisRoute==0) drop a
    // voxel whose OWN world column is FULLY fog-hidden (reveal <= 0) so FOG_TO_TRIXEL
    // can't hard-black its faces; STAGE_2 inherits the drop via its depth re-test. A
    // partially-revealed boundary column rasterizes so FOG_TO_TRIXEL fades the
    // silhouette like the floor (Mode B). The per-axis rotation routes run the SAME
    // reveal<=0 clip inside their branch below (#2128) rather than here, so the shared
    // pre-split code stays byte-identical to the pre-#2128 per-axis store (a
    // perAxisRoute!=0 early-return here reshuffles the Metal compiler's per-axis store
    // scheduling → tie-winner drift); the explicit perAxisRoute==0 term keeps this an
    // unconditional no-op on routes 1/2/3. `fogActive` covers the GRID + world-placed
    // re-voxelize detached canvas and short-circuits every other route, keeping
    // non-fog scenes byte-identical.
    // The GRID canvas keeps a disc-adjacent RING of hidden columns
    // (fogColumnRevealNearest's kFogHiddenKeepCells) so FOG_TO_TRIXEL's image-space
    // cut (#2124) has hidden matter to repaint per pixel. A world-placed DETACHED
    // canvas carries no fog pass, so nothing reclaims those kept columns — it clips
    // tight at the voxel lattice (fogColumnReveal <= 0), the #2137-era behaviour the
    // per-axis routes also apply. See #2248.
    const bool ownColumnHidden = frameData.isDetachedCanvas > 0.5f
        ? fogColumnReveal(canvasFogOfWar, fogObservers, worldColumn) <= 0.0f
        : fogColumnRevealNearest(canvasFogOfWar, fogObservers, worldColumn) <= 0.0f;
    if (fogActive && frameData.perAxisRoute == 0 && ownColumnHidden) {
        return;
    }

    // Per-slot deformation matrix — see stage 1 GLSL for the contract.
    const float2x2 D = float2x2(
        frameData.faceDeform[slot].xy,
        frameData.faceDeform[slot].zw
    );

    // Smooth camera Z-yaw per-axis routing (T2 / #1309 + T3 / #1310) — see
    // c_voxel_to_trixel_stage_1.glsl for the full contract. perAxisRoute==0
    // falls through to the byte-identical single-canvas path below. T3 stores
    // ONE cell per face center (not the emitDeformedFace cluster); atomicMin
    // resolves occlusion per cell and the framebuffer scatter reconstructs the
    // deformed face quad, so D is no longer applied here.
    if (frameData.perAxisRoute != 0) {
        // Per-axis own-column fog clip (#2128): the same #2102 + #2126 P2 drop as the
        // single-canvas route above (reveal <= 0 — FULLY hidden), applied on EVERY
        // axis route (1/2/3) so a rotating boundary object clips its hidden half
        // identically (a hidden column's Z-face would otherwise float on route 3).
        // Lives inside the per-axis branch so the shared pre-split path is byte-
        // identical to the pre-#2128 per-axis store; visionCircleCount==0 / the 1×1
        // placeholder short-circuit, so non-fog rotating scenes stay byte-identical.
        if (fogObservers.visionCircleCount > 0 &&
            fogColumnReveal(canvasFogOfWar, fogObservers, roundHalfUp(voxelPosition.xyz).xy) <= 0.0f) {
            return;
        }
        const int axis = frameData.perAxisRoute - 1;
        if ((faceId >> 1) != axis) return;
        // Un-yawed (cardinal) iso store: key each face by its
        // cardinal iso pixel `perAxisBase + pos3DtoPos2DIso(facePos)` instead of
        // the in-plane (y,z)/(x,z)/(x,y) lattice. The in-plane lattice collapses
        // faces that share an in-plane column but differ in depth-along-the-fixed
        // axis (separate objects stacked along x) onto one cell -> back face
        // dropped even though screen-separated. The cardinal iso key depends on
        // all three coords, so screen-separated faces land in distinct cells and
        // both survive; collisions occur only for genuine same-pixel cardinal
        // occlusion (resolved by the rawDepth atomicMin). The scatter recovers
        // the origin via isoPixelToPos3D (exact, non-singular at every yaw since
        // the index is un-yawed) and reprojects under the live yaw.
        // Whole-iso base anchor (#1944): per-axis store is base-resolution, so the
        // anchor must NOT be density-scaled (the scaled anchor jittered under pan —
        // see the #1944 NOTE in ir_iso_common). Cardinal single-canvas paths below
        // keep trixelFrameOffset.
        const int2 perAxisBase = trixelOriginOffsetZ1(frameData.canvasSizePixels) +
                                 int2(floor(frameData.frameCanvasOffset));
        if (frameData.voxelRenderOptions.x == 0) {
            const float3 worldAlignedBase = snapNearIntegerVoxelPosition(voxelPosition.xyz);
            const int3 worldPos = roundHalfUp(worldAlignedBase);
            const int3 facePos = faceMicroPositionFixed6(faceId, worldPos, 0, 0, 1);
            // Full sub-cell fracs (u/v in-plane + w out-of-plane) so a
            // fractionally-positioned face reconstructs on its TRUE plane,
            // not the integer lattice plane. Integer content encodes 8/8/8
            // (zero offsets) — byte-identical to the old centre-frac store.
            const float3 fracInCellBase = worldAlignedBase - float3(worldPos);
            const int voxelDistance = encodeDepthWithFaceFrac(
                pos3DtoDistance(facePos), slot, axis, fracInCellBase, riserFlip
            );
            if (frameData.resolveMode == 2) {
                viewMaskTap(perAxisBase, facePos, frameData, perAxisWinnerIds);
                return;
            }
            if (frameData.resolveMode == 3) {
                overflowAppendTap(
                    perAxisBase, facePos, voxelDistance, voxels[voxelIndex].colorPacked,
                    frameData, distanceScratch, perAxisWinnerIds, canvasSize
                );
                return;
            }
            if (frameData.resolveMode != 0) {
                resolveWinnerTap(
                    perAxisBase + pos3DtoPos2DIso(facePos), voxelDistance, voxelIndex,
                    distanceScratch, perAxisWinnerIds, canvasSize
                );
                return;
            }
            writeDistanceTap(
                perAxisBase + pos3DtoPos2DIso(facePos), voxelDistance,
                distanceScratch, canvasSize
            );
            return;
        }
        // #1458: store at BASE (world-unit) resolution regardless of effSub.
        // Only the z=0 invocation writes; higher z-slices return early.
        if (zIdx != 0) return;
        const float3 worldAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
        const int3 worldPos_sub = roundHalfUp(worldAligned);
        const int3 facePos_sub = faceMicroPositionFixed6(faceId, worldPos_sub, 0, 0, 1);
        const float3 fracInCell = worldAligned - float3(worldPos_sub);
        const int voxelDistance =
            encodeDepthWithFaceFrac(pos3DtoDistance(facePos_sub), slot, axis, fracInCell, riserFlip);
        if (frameData.resolveMode == 2) {
            viewMaskTap(perAxisBase, facePos_sub, frameData, perAxisWinnerIds);
            return;
        }
        if (frameData.resolveMode == 3) {
            overflowAppendTap(
                perAxisBase, facePos_sub, voxelDistance, voxels[voxelIndex].colorPacked,
                frameData, distanceScratch, perAxisWinnerIds, canvasSize
            );
            return;
        }
        // #2255: the 4-bit frac quantization above is where equal keys arise —
        // see the GLSL twin. The winner election keeps stage 2's color tap
        // deterministic among the tied faces.
        if (frameData.resolveMode != 0) {
            resolveWinnerTap(
                perAxisBase + pos3DtoPos2DIso(facePos_sub), voxelDistance, voxelIndex,
                distanceScratch, perAxisWinnerIds, canvasSize
            );
            return;
        }
        writeDistanceTap(
            perAxisBase + pos3DtoPos2DIso(facePos_sub), voxelDistance,
            distanceScratch, canvasSize
        );
        return;
    }

    if (frameData.voxelRenderOptions.x == 0) {
        // roundHalfUp, not hardware round(): half-integer voxel positions must
        // resolve to the same cell here, in stage 2's re-derivation, and in the
        // CPU-side IRMath::roundHalfUp consumers — hardware round() ties are
        // implementation-defined and leave a one-cell seam along tie planes.
        int3 voxelPositionInt = roundHalfUp(voxelPosition.xyz);
        if (cardinalIndex != 0) {
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
            voxelPositionInt += cardinalLowerCornerShift(cardinalIndex);
        }
        // Detached entities raster in model space; project occlusion depth
        // onto the entity-rotated iso axis (#1462). World/GRID keeps the fixed
        // (1,1,1) via pos3DtoDistance — byte-identical. See the GLSL twin.
        const int rawDepth = frameData.isDetachedCanvas > 0.5f
            ? isoDepthAlongAxis(voxelPositionInt, frameData.voxelDepthAxis.xyz)
            : pos3DtoDistance(voxelPositionInt);
        const int voxelDistance = encodeDepthWithFace(rawDepth, slot, riserFlip);
        const int2 base =
            trixelFrameOffset(
                frameData.trixelCanvasOffsetZ1,
                frameData.frameCanvasOffset,
                frameData.voxelRenderOptions
            ) +
            pos3DtoPos2DIso(voxelPositionInt);
        emitDeformedFace(base, D, voxelDistance, localId, frameData.isDetachedCanvas > 0.5f, faceId, reVoxelize, distanceScratch, canvasSize);
        return;
    }

    const int subdivisions = max(frameData.voxelRenderOptions.y, 1);
#if IR_FEEDER_PASS
    // #2258 Step B: strided feeder micro-grid — a coarser STRIDED SUBSET of the
    // full [0,subdivisions)² face cells (integer (i*subdivisions)/cap, monotone
    // + full-span; cap == subdivisions degenerates to the visible identity).
    // Geometry stays in `subdivisions` units — only sampling density drops.
    const int u = ((zIdx / feederCap) * subdivisions) / feederCap;
    const int v = ((zIdx % feederCap) * subdivisions) / feederCap;
#else
    const int u = zIdx / subdivisions;
    const int v = zIdx % subdivisions;
#endif

    const float3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const int3 voxelPositionFixed = roundHalfUp(voxelPositionAligned * float(subdivisions));
    const int2 frameOffsetFixed = trixelFrameOffset(
        frameData.trixelCanvasOffsetZ1,
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions
    );

    // View-space micro position at non-zero cardinals (#2424) — mirror of
    // c_voxel_to_trixel_stage_1_body.glsl: rotate the CELL origin (cell-index
    // map, hence the lower-corner shift; per-world-unit, scaled to match
    // `voxelPositionFixed = round(worldPos * subdivisions)`) and the FACE ID,
    // then run cardinal-0 face math on the pair. Rotating a world-computed
    // face plane after the fact applies the cell-index shift to a plane
    // BOUNDARY — the 1-sub-unit POS-face seam at cardinals 1/2/3.
    int3 viewCellFixed = voxelPositionFixed;
    int viewFaceId = faceId;
    if (cardinalIndex != 0) {
        viewCellFixed = rotateCardinalZ(voxelPositionFixed, cardinalIndex) +
            cardinalLowerCornerShift(cardinalIndex) * subdivisions;
        viewFaceId = rotateFaceIdCardinalZ(faceId, cardinalIndex);
    }
    const int3 microPositionFixed =
        faceMicroPositionFixed6(viewFaceId, viewCellFixed, u, v, subdivisions);
    // Detached entities project occlusion depth onto the entity-rotated iso
    // axis (#1462); world/GRID keeps the (x+y+z) fixed-(1,1,1) form. Depth is
    // in subdivision units on both branches, so the encode scale is unchanged.
    const int depthBase = frameData.isDetachedCanvas > 0.5f
        ? isoDepthAlongAxis(microPositionFixed, frameData.voxelDepthAxis.xyz)
        : (microPositionFixed.x + microPositionFixed.y + microPositionFixed.z);
    const int voxelDistance = encodeDepthWithFace(depthBase, slot, riserFlip);
    const int2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(base, D, voxelDistance, localId, frameData.isDetachedCanvas > 0.5f, viewFaceId, reVoxelize, distanceScratch, canvasSize);

    // Both-exposed dual emit (#2157): the opposite face plane rasters its own
    // pixels here (faceMicroPositionFixed6 is polarity-dependent), so the riser
    // needs its own deformed-face emit — same view-space form as the primary.
    // See the GLSL twin.
    if (bothPolaritiesExposed) {
        const int3 microOpposite =
            faceMicroPositionFixed6(viewFaceId ^ 1, viewCellFixed, u, v, subdivisions);
        const int depthOpposite = frameData.isDetachedCanvas > 0.5f
            ? isoDepthAlongAxis(microOpposite, frameData.voxelDepthAxis.xyz)
            : (microOpposite.x + microOpposite.y + microOpposite.z);
        // The opposite plane is the non-triplet polarity (GLSL twin).
        const int distanceOpposite = encodeDepthWithFace(depthOpposite, slot, riserFlip ^ 1);
        const int2 baseOpposite = frameOffsetFixed + pos3DtoPos2DIso(microOpposite);
        emitDeformedFace(baseOpposite, D, distanceOpposite, localId, frameData.isDetachedCanvas > 0.5f, viewFaceId ^ 1, reVoxelize, distanceScratch, canvasSize);
    }
}
