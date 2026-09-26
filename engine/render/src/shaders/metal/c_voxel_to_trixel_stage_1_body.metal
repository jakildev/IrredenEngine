// Shared stage-1 compute body — Metal twin of c_voxel_to_trixel_stage_1_body.glsl.
// An include fragment, not a standalone shader: each wrapper supplies the
// prerequisite includes and defines IR_FEEDER_PASS, IR_STORE_WINNER_ELECTION and
// IR_STAGE1_KERNEL_NAME, then includes this body. The wrappers:
//   c_voxel_to_trixel_stage_1.metal        → FEEDER 0, ELECTION 0 (visible dispatch)
//   c_voxel_to_trixel_stage_1_feeder.metal → FEEDER 1, ELECTION 0 (shadow-feeder dispatch)
//   c_voxel_to_trixel_stage_1_winner_resolve.metal
//                                          → FEEDER 0, ELECTION 1 (cardinal winner election)
// Variant-only code sits under `#if IR_FEEDER_PASS` / `#if IR_STORE_WINNER_ELECTION`
// so the visible kernel compiles with both variants' branches textually absent — no
// runtime predication tax on the hottest kernel. The body is kept include-free to
// mirror the GLSL twin's wrapper-supplied, macro-ordered include chain.

// MSL has no portable image-atomic syntax across all macOS versions, so the
// distance store goes through a sibling scratch buffer the same size as the
// R32I distance texture.

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

// Winner-resolve tap (resolveMode == 1) — GLSL twin in
// c_voxel_to_trixel_stage_1_body.glsl. Among the faces whose encoded distance
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

// View-visibility overflow lane — GLSL twin in
// c_voxel_to_trixel_stage_1_body.glsl. Yawed-depth quantization shared by the
// mask write (in the mode-0 store) and the resolveMode-3 mask compare:
// 1/16-world-unit steps, biased to a uint so atomic-min orders negative depths
// correctly. Both modes call THE SAME function on the SAME facePos, so a face
// always ties its own mask entry exactly regardless of float rounding.
constant float kOverflowDepthQuantScale = 16.0f;
// Half a world unit of tolerance (8 sixteenth-steps): absorbs quantization
// ties between genuinely co-visible faces without admitting occluded coset
// losers (the nearest coset pair separates by >= ~2.7 world units of yawed
// depth). Over-emit is safe — the framebuffer depth test cleans up; under-emit
// leaves holes where a view-visible face is missing.
constant uint kOverflowDepthEpsSteps = 8u;
constant int kOverflowDepthBias = 0x40000000;

inline uint overflowYawedDepthKey(int3 facePos, float visualYaw) {
    return uint(
        int(floor(yawedIsoDistanceCellAnchor(float3(facePos), visualYaw) * kOverflowDepthQuantScale)) +
        kOverflowDepthBias
    );
}

// The face's screen cell at the LIVE yaw, on the same perAxisBase anchor the
// cardinal store uses (the scatter projects with the identical cell-anchor
// projection, so mask cells and scattered quads agree).
inline int2 overflowYawedPixel(int2 perAxisBase, int3 facePos, float visualYaw) {
    return perAxisBase + roundHalfUp(pos3DtoPos2DIsoYawedCellAnchor(float3(facePos), visualYaw));
}

// View-mask write, run inside the resolveMode-0 store pass. Every per-axis face
// (all three axis routes — view visibility competes across axes) atomic-mins its
// quantized yawed depth into the shared mask region of the buffer-28 scratch.
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
// the cardinal-keyed store drops. Entries carry the exact (cardinal cell,
// encoded distance) pair the store would have written plus the raw colorPacked,
// so the scatter's overflow branch reuses the per-cell recovery bit-for-bit.
// Entry words are written with relaxed atomic stores — the scratch is declared
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
    // Compare the face's key against the MOST PERMISSIVE (largest) mask winner
    // over the 2x2 cell neighborhood spanning the UNROUNDED yawed position, not
    // the single roundHalfUp cell. A footprint straddling a cell boundary flips
    // its rounded cell (and thus its single-cell winner) frame-to-frame under
    // yaw; roundHalfUp(p) always lies in that 2x2 neighborhood, so the
    // neighborhood max removes the discontinuity while erring toward append
    // (over-emit loses the depth test; under-emit leaves holes). The mask WRITE
    // side stays the single roundHalfUp cell, so max() only admits a superset of
    // the single-cell pass and the write/compare self-tie holds.
    const float2 yawedPosRel =
        pos3DtoPos2DIsoYawedCellAnchor(float3(facePos), frameData.visualYaw);
    const int2 neighborhoodBase = perAxisBase + int2(floor(yawedPosRel));
    bool anyInside = false;
    uint maxMaskKey = 0u;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            const int2 neighborPix = neighborhoodBase + int2(dx, dy);
            if (!isInsideCanvas(neighborPix, frameData.canvasSizePixels)) {
                continue;
            }
            const uint neighborCell =
                uint(neighborPix.y) * uint(frameData.canvasSizePixels.x) + uint(neighborPix.x);
            maxMaskKey = max(maxMaskKey, atomic_load_explicit(
                &scratch[uint(frameData.overflowScratchLayout.x) + neighborCell],
                memory_order_relaxed
            ));
            anyInside = true;
        }
    }
    if (!anyInside) {
        return; // off-screen at the live yaw (whole footprint off-canvas)
    }
    // Wrap-safe occlusion test: an unwritten neighborhood cell reads the
    // 0xFFFFFFFF empty sentinel, so `maxMaskKey + eps` would wrap — compare in the
    // `key - eps` form (the key is bias-centered at ~0x40000000, eps=8u, no
    // underflow), treating the sentinel as infinitely permissive.
    if (overflowYawedDepthKey(facePos, frameData.visualYaw) - kOverflowDepthEpsSteps >
        maxMaskKey) {
        return; // view-occluded — nearer faces own the whole footprint neighborhood
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
// KEEP IN SYNC with c_voxel_visibility_compact.{glsl,metal} voxelOccludedByHiZ:
// the per-voxel Hi-Z occlusion cull's sampled window MUST be a conservative
// superset of this function's write set (`base + roundHalfUp(D * src)` over the
// [0,2)x[0,3) invocation lattice) so a visible voxel's own last-frame write always
// lands in the window it is tested against. Widening this emission hull without
// widening that window false-culls voxels (static-scene silhouette holes).
// Under IR_STORE_WINNER_ELECTION every distance tap in this function becomes a
// resolveWinnerTap, so the election dispatch's footprint equals the store's by
// construction — a missed site would leave winner == 0xFFFFFFFF at a tapped
// pixel and stage 2's guard would reject ALL writers there (a colour hole).
// KEEP IN SYNC with kGpuMargin, the shadow-feeder classify margin in
// system_voxel_to_trixel.hpp: stage 2's depth-only feeder skip is safe only
// because this write set stays INSIDE that margin. On the cardinal world route
// (identity D, n == 1) the set is base + {0,1}x{0,1,2} — reach +1 texel in x,
// +2 in y, 0 toward -x/-y — against a 4-texel margin. Overrunning THIS bound
// makes an on-screen pixel resolve from a voxel whose colour tap stage 2
// skipped. Gate: scripts/feeder-margin-verify.py (GL host).
inline void emitDeformedFace(
    int2 base,
    float2x2 D,
    int voxelDistance,
    uint2 localId,
    bool isDetached,
    int faceId,
    bool dilate,
    device atomic_int* distanceScratch,
    int2 canvasSize
#if IR_STORE_WINNER_ELECTION
    , uint voxelIndex
    , device atomic_uint* perAxisWinnerIds
#endif
) {
    const int maxN = isDetached ? 6 : 2;
    const int n = clamp(int(ceil(max(length(D[0]), length(D[1])))), 1, maxN);
    const float inv = 1.0 / float(n);
    // Conservative coverage — mirror of the GLSL twin. A re-voxelize canvas
    // bakes the rotation into integer CELL positions, so round-to-cell leaves
    // sub-cell gaps; each surface face dilates ±1px along its in-plane iso axes
    // so the gaps fill with the nearest face (atomicMin keeps the occlusion
    // winner; stage 2's depth re-test paints the matching colour).
    int2 su = int2(0);
    int2 sv = int2(0);
    if (dilate) {
        faceInPlaneIsoSteps(faceId, su, sv);
    }
    for (int sy = 0; sy < n; ++sy) {
        for (int sx = 0; sx < n; ++sx) {
            const float2 src = float2(localId) + float2(float(sx), float(sy)) * inv;
            const int2 p = base + roundHalfUp(D * src);
#if IR_STORE_WINNER_ELECTION
            resolveWinnerTap(p, voxelDistance, voxelIndex, distanceScratch, perAxisWinnerIds, canvasSize);
            if (dilate) {
                resolveWinnerTap(p + su, voxelDistance, voxelIndex, distanceScratch, perAxisWinnerIds, canvasSize);
                resolveWinnerTap(p - su, voxelDistance, voxelIndex, distanceScratch, perAxisWinnerIds, canvasSize);
                resolveWinnerTap(p + sv, voxelDistance, voxelIndex, distanceScratch, perAxisWinnerIds, canvasSize);
                resolveWinnerTap(p - sv, voxelDistance, voxelIndex, distanceScratch, perAxisWinnerIds, canvasSize);
            }
#else
            writeDistanceTap(p, voxelDistance, distanceScratch, canvasSize);
            if (dilate) {
                writeDistanceTap(p + su, voxelDistance, distanceScratch, canvasSize);
                writeDistanceTap(p - su, voxelDistance, distanceScratch, canvasSize);
                writeDistanceTap(p + sv, voxelDistance, distanceScratch, canvasSize);
                writeDistanceTap(p - sv, voxelDistance, distanceScratch, canvasSize);
            }
#endif
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
// exposed-face test (visible-triplet × exposed-mask) is centralized in
// `faceIsExposed(flagsByte, faceId)` from ir_iso_common.metal.

// Fog constants/struct + fogColumnReveal/Nearest and the shared
// face-selection / per-axis store-key math come from ir_voxel_face_select.metal.
// STAGE_1's fog grid rides [[texture(0)]] — free here, the distance store goes
// through the atomic scratch buffer; Metal passes the texture + observers into
// the shared functions as arguments.

// Z-cost twin of fogColumnReveal for the own-column drop on the routes
// FOG_TO_TRIXEL never paints — the DETACHED canvas and the per-axis rotation
// textures (the voxel's world Z is known there). It folds the per-circle height
// penalty zCostUp * max(dzUp - freeBand, 0) + zCostDown * max(dzDown -
// freeBand, 0), where dzUp = max(observerZ - voxelZ, 0) and dzDown =
// max(voxelZ - observerZ, 0), into the effective radial distance, so a
// height-hidden voxel is removed on the z-aware curve FOG_TO_TRIXEL reveals by.
// The world canvas does NOT use it: its drop is z-free so a height-hidden voxel
// keeps its geometry and the fog pass paints it unexplored. Mirror of the GLSL twin. It lives HERE
// rather than beside the z-free twins in ir_voxel_face_select.metal because the
// drop is STAGE-1-ONLY — stage 2 never repeats it — and the shared include
// holds exactly the definitions both stages must agree on. The disc test itself
// is fogDiscRevealAtDistance, the one fogColumnReveal uses, so the drop and the
// cut-face rule resolve a column on a hard disc's rim the same way. All-zero
// heights make this return exactly fogColumnReveal's value.
static float fogColumnRevealZ(
    texture2d<float, access::read> fog, constant FogObserverData& obs, int2 col, float voxelZ
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
        const float4 h = obs.visionCircleHeights[i];
        const float dzUp = max(h.x - voxelZ, 0.0f);
        const float dzDown = max(voxelZ - h.x, 0.0f);
        const float distEff = length(float2(col) - obs.visionCircles[i].xy) +
            h.y * max(dzUp - h.w, 0.0f) + h.z * max(dzDown - h.w, 0.0f);
        reveal = max(
            reveal,
            fogDiscRevealAtDistance(distEff, obs.visionCircles[i].z, obs.visionCircles[i].w)
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

    // Micro-slice packing — mirrors the GLSL twin. The threadgroup z-size is
    // kStageMicroSlicesPerGroup (metal_pipeline.cpp map); recover this
    // invocation's flat micro-slice index and discard the tail past
    // microSliceCount.
    const int zIdx =
        int(groupId.z) * kStageMicroSlicesPerGroup + int(localId3.z);
#if IR_FEEDER_PASS
    // The feeder dispatch (struct 1) rasters feederSubCap² micro-cells per face
    // instead of effSub²; the guard must match the compact's writeDispatchDims
    // z-count for this pass.
    const int feederCap = max(frameData.feederSubCap, 1);
    const int microSliceCount =
        (frameData.voxelRenderOptions.x != 0) ? (feederCap * feederCap) : 1;
#else
    const int microSliceCount = (frameData.voxelRenderOptions.x != 0 && frameData.perAxisRoute == 0)
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
    // `slot` is the per-voxel visible-triplet index (0/1/2); `faceId` is the
    // WORLD FaceId (0..5) the camera sees at this slot.
    const int slot = localIDToFace_2x3(localId);
    int faceId = frameData.visibleFaceIds[slot];
    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);

    const int2 canvasSize = frameData.canvasSizePixels;

    // Re-voxelize marker: detached canvases (visibleFaceIds.w != 0) bake the
    // entity rotation into the CELL positions and raster at cardinal 0.
    const bool reVoxelize = frameData.visibleFaceIds.w != 0;
    const bool dilateRevox = frameData.visibleFaceIds.w == 1;

    // Exposed-face gate: emit only faces that are camera-visible AND exposed.
    // Re-voxelize canvases gate too: c_revoxelize_detached authors the
    // ROTATED-frame exposed mask from dest-grid adjacency, so `flags_` is valid
    // in the rotated frame.
    const uint flagsByte = (voxels[voxelIndex].materialFlagBone >> 8u) & 0xFFu;

    // Face selection — the visible-triplet × exposed-mask gate, the
    // silhouette-riser flip + dual-emit predicate for rotated content, and the
    // fog cut-face widening — is shared with stage 2 via
    // ir_voxel_face_select.metal: both stages key their taps off ONE definition,
    // so the colour tap cannot desync from the distance tap.
    const VoxelFaceSelect sel = selectVoxelFace(
        canvasFogOfWar, fogObservers, faceId, reVoxelize,
        voxels[voxelIndex].reserved, flagsByte, voxelPosition,
        frameData.perAxisRoute, frameData.isDetachedCanvas,
        frameData.detachedWorldReceive
    );
    if (!sel.keepFace) return;
    faceId = sel.faceId;
    const int riserFlip = sel.riserFlip;
    const bool bothPolaritiesExposed = sel.bothPolaritiesExposed;
    const bool fogWholeBodyExempt =
        (voxels[voxelIndex].reserved & (1u << 3u)) != 0u;

    // Per-voxel analytic fog clip — STAGE-1-ONLY (stage 2 never repeats it:
    // with the distances dropped, its colour taps are rejected by the depth
    // re-test). Drops a voxel whose OWN world column is FULLY hidden
    // (reveal <= 0); a partially revealed column is kept so FOG_TO_TRIXEL fades
    // the silhouette. The GRID canvas keeps a hidden ring via
    // fogColumnRevealNearest so the fog cut has matter to repaint; a
    // world-placed DETACHED canvas has no fog pass and clips tight at the voxel
    // lattice. The per-axis routes run their own clip inside their branch.
    // The GRID drop is z-FREE: a voxel above the height ceiling but inside the
    // disc keeps its geometry and FOG_TO_TRIXEL paints it the unexplored colour
    // per pixel — removing it would expose the ground through a hollow, shorter
    // body. The detached canvas, which has no paint pass, drops on its own
    // world Z. Mirror of the GLSL twin.
    const bool ownColumnHidden = frameData.isDetachedCanvas > 0.5f
        ? fogColumnRevealZ(canvasFogOfWar, fogObservers, sel.worldColumn, voxelPosition.z) <= 0.0f
        : fogColumnRevealNearest(canvasFogOfWar, fogObservers, sel.worldColumn) <= 0.0f;
    if (!fogWholeBodyExempt && sel.fogActive &&
        frameData.perAxisRoute == 0 && ownColumnHidden) {
        return;
    }

    // Per-slot deformation matrix, indexed by visible-triplet slot; identity at
    // cardinal 0 with residualYaw == 0.
    if (frameData.isDetachedCanvas > 0.5 && !reVoxelize) {
        if (any(int2(localId) != faceOffset_2x3(slot, 0))) return;
        const int density = frameData.voxelRenderOptions.x != 0 ? max(frameData.voxelRenderOptions.y, 1) : 1;
        const DetachedFaceFootprint face = detachedFaceFootprint(
            voxelPosition.xyz, faceId, density, zIdx,
            float2x2(frameData.faceDeform[0].xy, frameData.faceDeform[0].zw),
            float2x2(frameData.faceDeform[1].xy, frameData.faceDeform[1].zw),
            frameData.voxelDepthAxis.xyz,
            trixelFrameOffset(frameData.trixelCanvasOffsetZ1, frameData.frameCanvasOffset, frameData.voxelRenderOptions)
        );
        const int parity = localTrixelOriginParity(frameData.trixelCanvasOffsetZ1);
        for (int y = max(face.lo.y, 0); y <= min(face.hi.y, frameData.canvasSizePixels.y - 1); ++y) {
            for (int x = max(face.lo.x, 0); x <= min(face.hi.x, frameData.canvasSizePixels.x - 1); ++x) {
                const int2 pixel = int2(x, y);
                const int depth = detachedFaceSampleDepth(face, pixel, parity, slot);
                if (depth == kDetachedFaceMissDepth) continue;
#if IR_STORE_WINNER_ELECTION
                resolveWinnerTap(pixel, depth, voxelIndex, distanceScratch, perAxisWinnerIds, canvasSize);
#else
                writeDistanceTap(pixel, depth, distanceScratch, canvasSize);
#endif
            }
        }
        return;
    }

    const float2x2 D = float2x2(
        frameData.faceDeform[slot].xy,
        frameData.faceDeform[slot].zw
    );

    // Smooth camera Z-yaw per-axis routing
    // (docs/design/per-axis-trixel-canvas-rotation.md). Each axis route stores
    // ONE cell per face center (not the emitDeformedFace cluster); atomicMin
    // resolves occlusion per cell and the framebuffer scatter reconstructs the
    // deformed face quad, so D is not applied here.
    if (frameData.perAxisRoute != 0) {
        // Per-axis own-column fog clip: reveal <= 0 (FULLY hidden), applied on
        // EVERY axis route (1/2/3) so a rotating boundary object clips its
        // hidden half identically (a hidden column's Z-face would otherwise float
        // on route 3). The nearest-cell metric keeps the hidden ring that the fog
        // pass paints on every per-axis sample. visionCircleCount==0 and the 1×1
        // placeholder grid short-circuit non-fog rotating scenes.
        if (!fogWholeBodyExempt && fogObservers.visionCircleCount > 0 &&
            fogColumnRevealNearest(
                canvasFogOfWar, fogObservers, roundHalfUp(voxelPosition.xyz).xy
            ) <= 0.0f) {
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
        // Whole-iso base anchor: per-axis store is base-resolution, so the
        // anchor must NOT be density-scaled (a density-scaled anchor jitters under
        // pan). Cardinal single-canvas paths keep trixelFrameOffset.
        const int2 perAxisBase = trixelOriginOffsetZ1(frameData.canvasSizePixels) +
                                 int2(floor(frameData.frameCanvasOffset));
        // Store at BASE (world-unit) resolution regardless of effSub —
        // on the subdivided path only the z=0 invocation writes (the voxel's
        // continuous sub-cell offset rides the encoding so the scatter can
        // sub-pixel-shift the face quad).
        if (frameData.voxelRenderOptions.x != 0 && zIdx != 0) return;
        int voxelDistance;
        const int3 facePos =
            perAxisStoreFacePos(voxelPosition, faceId, slot, axis, riserFlip, voxelDistance);
        if (frameData.resolveMode == 3) {
            // Each record reconstructs the whole face, not one of its two trixels.
            if (any(int2(localId) != faceOffset_2x3(slot, 0))) return;
            const uint fogClassByte = fogWholeBodyExempt ? 254u : 255u;
            const uint overflowColor =
                (voxels[voxelIndex].colorPacked & 0x00FFFFFFu) | (fogClassByte << 24u);
            overflowAppendTap(
                perAxisBase, facePos, voxelDistance, overflowColor,
                frameData, distanceScratch, perAxisWinnerIds, canvasSize
            );
            return;
        }
        // Equal keys arise from perAxisStoreFacePos's 4-bit frac quantization —
        // the winner election here is what keeps the stage-2 color tap
        // deterministic among them.
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
        viewMaskTap(perAxisBase, facePos, frameData, perAxisWinnerIds);
        return;
    }

    if (frameData.voxelRenderOptions.x == 0) {
        // roundHalfUp, not hardware round(): half-integer voxel positions must
        // resolve to the same cell here, in stage 2's re-derivation, and in the
        // CPU-side IRMath::roundHalfUp consumers — hardware round() ties are
        // implementation-defined and leave a one-cell seam along tie planes.
        int3 voxelPositionInt = roundHalfUp(voxelPosition.xyz);
        if (cardinalIndex != 0) {
            // Plain cardinal rotation — no lower-corner shift, which would
            // rotate the mass about its lower-corner lattice (anchor p + 0.5)
            // and orbit any pinned focus. Matches the GLSL twin.
            voxelPositionInt = rotateCardinalZ(voxelPositionInt, cardinalIndex);
        }
        // Detached entities raster in model space; project occlusion depth
        // onto the entity-rotated iso axis. World/GRID keeps the fixed
        // (1,1,1) via pos3DtoDistance.
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
        emitDeformedFace(
            base, D, voxelDistance, localId, frameData.isDetachedCanvas > 0.5f, faceId,
            dilateRevox, distanceScratch, canvasSize
#if IR_STORE_WINNER_ELECTION
            , voxelIndex, perAxisWinnerIds
#endif
        );
        return;
    }

    const int subdivisions = max(frameData.voxelRenderOptions.y, 1);
#if IR_FEEDER_PASS
    // Strided feeder micro-grid — a coarser STRIDED SUBSET of the full
    // [0,subdivisions)² face cells (integer (i*subdivisions)/cap, monotone
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

    // View-space micro position at non-zero cardinals — mirror of
    // c_voxel_to_trixel_stage_1_body.glsl: rotate the CELL origin and the
    // FACE ID, then run cardinal-0 face math on the pair. Rotating a
    // world-computed face plane after the fact treats a plane BOUNDARY as a
    // cell index — a 1-sub-unit POS-face seam at cardinals 1/2/3.
    int3 viewCellFixed = voxelPositionFixed;
    int viewFaceId = faceId;
    if (cardinalIndex != 0) {
        // Plain cardinal rotation — no lower-corner shift; the whole
        // cell grid translates uniformly so face adjacency is preserved.
        // Matches the GLSL twin.
        viewCellFixed = rotateCardinalZ(voxelPositionFixed, cardinalIndex);
        viewFaceId = rotateFaceIdCardinalZ(faceId, cardinalIndex);
    }
    const int3 microPositionFixed =
        faceMicroPositionFixed6(viewFaceId, viewCellFixed, u, v, subdivisions);
    // Detached entities project occlusion depth onto the entity-rotated iso
    // axis; world/GRID keeps the (x+y+z) fixed-(1,1,1) form. Depth is
    // in subdivision units on both branches, so the encode scale is unchanged.
    const int depthBase = frameData.isDetachedCanvas > 0.5f
        ? isoDepthAlongAxis(microPositionFixed, frameData.voxelDepthAxis.xyz)
        : (microPositionFixed.x + microPositionFixed.y + microPositionFixed.z);
    const int voxelDistance = encodeDepthWithFace(depthBase, slot, riserFlip);
    const int2 base = frameOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
    emitDeformedFace(
        base, D, voxelDistance, localId, frameData.isDetachedCanvas > 0.5f, viewFaceId,
        dilateRevox, distanceScratch, canvasSize
#if IR_STORE_WINNER_ELECTION
        , voxelIndex, perAxisWinnerIds
#endif
    );

    // Both-exposed dual emit: the opposite face plane rasters its own
    // pixels here (faceMicroPositionFixed6 is polarity-dependent), so the riser
    // needs its own deformed-face emit — same view-space form as the primary.
    // `viewFaceId ^ 1` after rotation == rotating the opposite face, since
    // rotateFaceIdCardinalZ maps opposite-face pairs to opposite-face pairs.
    if (bothPolaritiesExposed) {
        const int3 microOpposite =
            faceMicroPositionFixed6(viewFaceId ^ 1, viewCellFixed, u, v, subdivisions);
        const int depthOpposite = frameData.isDetachedCanvas > 0.5f
            ? isoDepthAlongAxis(microOpposite, frameData.voxelDepthAxis.xyz)
            : (microOpposite.x + microOpposite.y + microOpposite.z);
        // The opposite plane is the non-triplet polarity of this slot.
        const int distanceOpposite = encodeDepthWithFace(depthOpposite, slot, riserFlip ^ 1);
        const int2 baseOpposite = frameOffsetFixed + pos3DtoPos2DIso(microOpposite);
        emitDeformedFace(
            baseOpposite, D, distanceOpposite, localId, frameData.isDetachedCanvas > 0.5f,
            viewFaceId ^ 1, dilateRevox, distanceScratch, canvasSize
#if IR_STORE_WINNER_ELECTION
            , voxelIndex, perAxisWinnerIds
#endif
        );
    }
}
