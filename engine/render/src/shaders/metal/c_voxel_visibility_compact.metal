#include "ir_iso_common.metal"
#include "ir_constants.metal"

// Voxel visibility + compaction pass.  Each thread inspects one voxel,
// performs chunk-level visibility, alpha rejection, and iso-bounds culling,
// then atomically appends its index into the compacted indices buffer.  The
// last group to finish (tracked via the completedGroups counter) writes the
// indirect dispatch params used by the stage 1 / stage 2 voxel-to-trixel
// passes.
//
// IndirectDispatchParams memory layout (matches the GLSL std430 SSBO):
//   [0] numGroupsX        (written by last group)
//   [1] numGroupsY        (written by last group)
//   [2] numGroupsZ        (written by last group)
//   [3] visibleCount      (atomic increment, read by stage 1 / stage 2)
//   [4] completedGroups   (atomic increment, last-group barrier)
// All five fields are accessed through a single `device atomic_uint*` so
// that we can mix atomic_fetch_add (3, 4) with atomic_store (0, 1, 2)
// without rebinding the buffer.
//
// memory_order_relaxed is intentional. The happens-before edge that makes
// the indirect-dispatch params visible to the following pipeline stage
// comes from the encoder boundary between this kernel and stage 1 — Metal
// orders device-memory operations between command-buffer encoders for us.
// Within this kernel, threadgroup_barrier(mem_device) handles intra-group
// ordering; the last-group pattern relies on atomic_fetch_add being a
// coherent RMW across threadgroups, not on C++ memory-order semantics.
// Don't "fix" these to acquire/release — it adds barrier cost on Apple
// GPUs without changing observed behavior.

constant uint kSlotNumGroupsX     = 0;
constant uint kSlotNumGroupsY     = 1;
constant uint kSlotNumGroupsZ     = 2;
constant uint kSlotVisibleCount   = 3;
constant uint kSlotCompletedGroups = 4;

// Per-axis store list-walk split (#1739). In single-canvas mode the indirect
// params buffer holds one struct (slots 0..4 above). In per-axis split mode
// (frameData.perAxisRoute != 0, carrying the region stride) it holds three
// dispatch structs spaced kPerAxisIndirectStrideUints (256 B) apart so the CPU
// can bindRange each at an SSBO-offset-aligned boundary; slot
// kSlotCompletedGroups of struct 0 is the shared cross-group completion counter
// in both modes. Mirrors c_voxel_visibility_compact.glsl + C++ kPerAxisSsboAlignBytes.
constant uint kPerAxisIndirectStrideUints = 64u;  // 256 B / 4

// 12 B per voxel — must match C_Voxel + the `Voxel` struct in
// c_voxel_to_trixel_stage_1.metal. Read ONLY in split mode for the
// face-occlusion flags byte (`materialFlagBone >> 8`).
struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};

// Indirect dispatch grid for `base`'s struct from its visibleCount slot (matches
// the single-canvas numGroups math exactly).
static void writeDispatchDims(
    device atomic_uint* indirectParams, uint base, int renderModeX, int subdivisionsY
) {
    const uint count = atomic_load_explicit(
        &indirectParams[base + kSlotVisibleCount], memory_order_relaxed
    );
    const uint gx = max(min(count, 1024u), 1u);
    atomic_store_explicit(&indirectParams[base + kSlotNumGroupsX], gx, memory_order_relaxed);
    atomic_store_explicit(
        &indirectParams[base + kSlotNumGroupsY],
        max((count + gx - 1u) / gx, 1u),
        memory_order_relaxed
    );
    const int subdivisions = max(subdivisionsY, 1);
    // #2258: pack kStageMicroSlicesPerGroup micro-cells per z-workgroup (the
    // stage kernels' threadgroup z-size), so the launched z-workgroup count is
    // the ceil-divided micro-slice count. Mirrors c_voxel_visibility_compact.glsl.
    const uint microSliceCount = (renderModeX != 0) ? uint(subdivisions * subdivisions) : 1u;
    const uint gz = (microSliceCount + uint(kStageMicroSlicesPerGroup) - 1u) /
        uint(kStageMicroSlicesPerGroup);
    atomic_store_explicit(&indirectParams[base + kSlotNumGroupsZ], gz, memory_order_relaxed);
}

// Fog-of-war column cull (#2008). Mirrors c_voxel_visibility_compact.glsl. The
// fog `.r` channel reads back in normalized space: unexplored 0.0, explored
// ≈0.5, visible 1.0. The world fog canvas binds its 256² fog texture at
// [[texture(0)]]; every non-fog canvas binds a 1×1 all-visible placeholder, so
// `get_width() <= 1` short-circuits the cull (byte-identical to master).
constant int kFogOfWarHalfExtent = 128;
constant float kFogExploredThreshold = 0.25f;

// True iff this voxel's RAW world column is unexplored. World-space fog grid →
// use voxelPosRaw (pre-cardinal-rotation). Out-of-range columns + the 1×1
// placeholder both return false (visible → no cull), matching c_fog_to_trixel.
static bool fogColumnUnexplored(
    texture2d<float, access::read> fog, int3 voxelPosRaw
) {
    const int2 fogSize = int2(int(fog.get_width()), int(fog.get_height()));
    if (fogSize.x <= 1) {
        return false;
    }
    const int2 fogCell = int2(
        voxelPosRaw.x + kFogOfWarHalfExtent,
        voxelPosRaw.y + kFogOfWarHalfExtent
    );
    if (fogCell.x < 0 || fogCell.x >= fogSize.x ||
        fogCell.y < 0 || fogCell.y >= fogSize.y) {
        return false;
    }
    return fog.read(uint2(fogCell)).r < kFogExploredThreshold;
}

// Live analytic fog vision circles — see the GLSL mirror. Std140/Metal-tight
// mirror of FrameDataFogObservers (component_canvas_fog_of_war.hpp); the
// system's per-frame observer upload verbatim. Bound at buffer(27)
// (kBufferIndex_FogObservers alias) by VOXEL_TO_TRIXEL_STAGE_1 right before this
// compact. The grid texture above carries only coarse explored/voxelized
// memory; these discs carry the smooth "currently visible". A column any disc
// covers survives the grid cull, so a voxel-floor scene driven purely by
// setVisionCircle keeps its floor (the grid-only cull would otherwise drop
// every unexplored column and black it out).
constant int kMaxFogVisionCircles = 8; // mirror of component_canvas_fog_of_war.hpp kMaxFogVisionCircles — must stay in sync
struct FogObserverData {
    float4 visionCircles[kMaxFogVisionCircles]; // (centerX, centerY, radius, edgeSoftness)
    int visionCircleCount;
    int _fogObsPad0;
    int _fogObsPad1;
    int _fogObsPad2;
};

// Safety margin (cells) — mirrors kCullSafetyCells in the GLSL: covers the
// per-pixel worldPerPixel AA in c_fog_to_trixel that this shader can't compute.
// Must stay a superset of stage 1's fog-hidden keep ring
// (kFogHiddenKeepCells + aa) — see the GLSL twin.
constant float kCullSafetyCells = 9.0f;

// All six face-occlusion bits of the voxel flags byte (bits [2..7], the
// shader-side mirror of IRComponents::VoxelFlags::kFaceOccludedMask). A
// flags byte matching the full mask marks a fully-interior voxel.
constant uint kFaceOccludedMaskBits = 0xFCu;

// Strict-behind margin (encoded units) so FMA / round noise on the boundary
// never culls a voxel only coplanar with the occluder. Must match
// kOcclusionDepthMargin in c_chunk_occlusion_cull.metal.
constant int kOcclusionDepthMargin = 4;

// Per-voxel Hi-Z occlusion refine (#1812) — see the GLSL twin
// (c_voxel_visibility_compact.glsl) for the full rationale: conservative,
// off by default (occlusionCullMipCount == 0 -> no-op), shadow-feeder-safe
// (only voxels inside the un-widened visibleIsoBounds are tested), background
// sentinel keeps a voxel that still sees background, and the encoding
// (pos3DtoDistance(voxelPos) * 4) matches dispatchChunkOcclusion's cb.minDepth_
// exactly. hiZLevel0 is the finest downsampled level (source px >> 1); a voxel's
// ~1 iso px footprint always lands there.
static bool voxelOccludedByHiZ(
    texture2d<int, access::read> hiZLevel0,
    constant FrameDataVoxelToTrixel& fd,
    int3 voxelPos,
    int2 isoPos
) {
    if (fd.occlusionCullMipCount <= 0) {
        return false;
    }
    if (isoPos.x < fd.visibleIsoBounds.x || isoPos.y < fd.visibleIsoBounds.y ||
        isoPos.x > fd.visibleIsoBounds.z || isoPos.y > fd.visibleIsoBounds.w) {
        return false;
    }
    const int2 canvasPixel =
        fd.trixelCanvasOffsetZ1 + int2(floor(fd.frameCanvasOffset)) + isoPos;
    const int2 texel = canvasPixel >> 1;
    const int2 sz = int2(int(hiZLevel0.get_width()), int(hiZLevel0.get_height()));
    int hiZMax = -2147483648;
    for (int ty = texel.y - 1; ty <= texel.y + 1; ++ty) {
        for (int tx = texel.x - 1; tx <= texel.x + 1; ++tx) {
            const int2 c = clamp(int2(tx, ty), int2(0), sz - int2(1));
            hiZMax = max(hiZMax, hiZLevel0.read(uint2(c)).x);
        }
    }
    return pos3DtoDistance(voxelPos) * 4 > hiZMax + kOcclusionDepthMargin;
}

// True iff this column lies under any live analytic vision circle, so its voxels
// survive the grid cull and FOG_TO_TRIXEL can reveal them smoothly per pixel.
// Uses a cell nearest-point (AABB) test (mirrors the GLSL): the voxel cell at
// integer (cx,cy) covers [cx-0.5, cx+0.5]×[cy-0.5, cy+0.5]; the residual after
// clamping is the distance to the nearest point of the AABB. Strictly more
// permissive than a center-only test, so columns whose nearest corner touches the
// smooth band rasterize — the per-pixel mask owns the visible edge at pixel
// resolution and no grid-aligned notches appear along the arc.
static bool fogColumnInVisionCircle(
    constant FogObserverData& obs, int3 voxelPosRaw
) {
    const float2 col = float2(voxelPosRaw.xy);
    for (int i = 0; i < obs.visionCircleCount; ++i) {
        float2 d = abs(col - obs.visionCircles[i].xy) - float2(0.5f);
        d = max(d, float2(0.0f));
        const float keepR =
            obs.visionCircles[i].z + max(obs.visionCircles[i].w, 0.0f) + kCullSafetyCells;
        if (dot(d, d) <= keepR * keepR) {
            return true;
        }
    }
    return false;
}

// T-287 / #950: this kernel no longer reads the per-voxel color SSBO. The
// per-slot active bit at `activeMask[idx >> 5] & (1 << (idx & 31))` is the
// CPU-pushed mirror of `m_voxelColors[idx].color_.alpha_ != 0`, kept in
// sync at mutation time. Replacing the alpha read with a 1-bit lookup
// halves the per-thread SSBO bandwidth on the dominant hollow / sparse
// cases (the visibility-test path) and lets inactive slots short-circuit
// before the 12 B color load.

kernel void c_voxel_visibility_compact(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    device const float4* positions [[buffer(5)]],
    device const Voxel* voxels [[buffer(6)]],
    device const uint* activeMask [[buffer(8)]],
    device const uint* chunkVisible [[buffer(24)]],
    device uint* compactedVoxelIndices [[buffer(25)]],
    device atomic_uint* indirectParams [[buffer(26)]],
    texture2d<float, access::read> canvasFogOfWar [[texture(0)]],
    // Finest Hi-Z level for the per-voxel occlusion cull (#1812). At texture(1)
    // so it never aliases the fog image at texture(0) in Metal's shared argument
    // table. Sampled only when frameData.occlusionCullMipCount > 0; otherwise a
    // never-sampled sentinel is bound so the argument table stays satisfied.
    texture2d<int, access::read> hiZLevel0 [[texture(1)]],
    constant FogObserverData& fogObservers [[buffer(27)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 groupCount [[threadgroups_per_grid]],
    uint3 localId [[thread_position_in_threadgroup]],
    uint localIndex [[thread_index_in_threadgroup]]
) {
    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    const uint workGroupIndex = groupId.x + groupId.y * groupCount.x;
    const uint idx = workGroupIndex * 64u + localId.x;

    if (idx < uint(frameData.voxelCount)) {
        const uint chunkIdx = idx / uint(VOXEL_CHUNK_SIZE);
        if (chunkVisible[chunkIdx] != 0u) {
            const uint maskWord = activeMask[idx >> 5u];
            if (((maskWord >> (idx & 31u)) & 1u) != 0u) {
                // roundHalfUp keeps tie cells consistent with the stage-1/2
                // raster (hardware round() ties are implementation-defined).
                const int3 voxelPosRaw = roundHalfUp(positions[idx].xyz);
                int2 isoPos;
                int cullMargin = 0;
                // Cardinal-rotated position, hoisted for the per-voxel occlusion
                // test (only rotated on the residual==0 path — the only path
                // where occlusionCullMipCount can be non-zero).
                int3 voxelPos = voxelPosRaw;
                // Smooth camera Z-yaw (T3 / #1310) — see the GLSL mirror for the
                // rationale: while rotating, project the cull with the same
                // continuous yaw the per-axis scatter raster uses (cardinal snap
                // drops off-center voxels), widened by the sqrt2 face footprint.
                // residual == 0 keeps the byte-identical cardinal-snap path.
                if (frameData.residualYaw != 0.0f) {
                    isoPos = roundHalfUp(
                        pos3DtoPos2DIsoYawed(float3(voxelPosRaw), frameData.visualYaw)
                    );
                    cullMargin = 2;
                } else {
                    if (cardinalIndex != 0) {
                        voxelPos = rotateCardinalZ(voxelPos, cardinalIndex);
                        voxelPos += cardinalLowerCornerShift(cardinalIndex);
                    }
                    isoPos = pos3DtoPos2DIso(voxelPos);
                }
                if (isoPos.x >= frameData.cullIsoMin.x - cullMargin &&
                    isoPos.x <= frameData.cullIsoMax.x + cullMargin &&
                    isoPos.y >= frameData.cullIsoMin.y - cullMargin &&
                    isoPos.y <= frameData.cullIsoMax.y + cullMargin &&
                    (!fogColumnUnexplored(canvasFogOfWar, voxelPosRaw) ||
                     fogColumnInVisionCircle(fogObservers, voxelPosRaw))) {
                    if (frameData.perAxisRoute == 0) {
                        // Fully-interior drop — mirrors the GLSL twin: all six
                        // face-occlusion bits set means stage 1/2 fail
                        // faceIsExposed on every slot and the rotated
                        // opposite-polarity emit has no exposed opposite, so
                        // skipping the append is output-identical and saves the
                        // voxel's sub²-slice stage-1 dispatch. Gated off while
                        // any fog vision circle is live (a circle can revive an
                        // occluded vertical face as a cut wall, #2125). No early
                        // return — the completion barrier below must stay
                        // uniformly reached.
                        // Per-voxel Hi-Z occlusion refine (#1812): drop a voxel
                        // globally occluded by closer geometry. Off by default;
                        // never re-adds a fully-interior voxel already dropped
                        // above. No early return — the completion barrier below
                        // must stay uniformly reached.
                        const uint flagsByte = (voxels[idx].materialFlagBone >> 8u) & 0xFFu;
                        if ((fogObservers.visionCircleCount != 0 ||
                             (flagsByte & kFaceOccludedMaskBits) != kFaceOccludedMaskBits) &&
                            !voxelOccludedByHiZ(hiZLevel0, frameData, voxelPos, isoPos)) {
                            const uint slot = atomic_fetch_add_explicit(
                                &indirectParams[kSlotVisibleCount],
                                1u,
                                memory_order_relaxed
                            );
                            compactedVoxelIndices[slot] = idx;
                        }
                    } else {
                        // Per-axis split (#1739): append into each axis region the
                        // voxel has an exposed face on. The store shader re-checks
                        // precise per-axis exposure, so over-inclusion is harmless;
                        // a fully-interior voxel lands in no region.
                        const uint flagsByte = (voxels[idx].materialFlagBone >> 8u) & 0xFFu;
                        const uint stride = uint(frameData.perAxisRoute);
                        for (int axis = 0; axis < 3; ++axis) {
                            if (faceIsExposed(flagsByte, 2 * axis) ||
                                faceIsExposed(flagsByte, 2 * axis + 1)) {
                                const uint base = uint(axis) * kPerAxisIndirectStrideUints;
                                const uint slot = atomic_fetch_add_explicit(
                                    &indirectParams[base + kSlotVisibleCount],
                                    1u,
                                    memory_order_relaxed
                                );
                                compactedVoxelIndices[uint(axis) * stride + slot] = idx;
                            }
                        }
                    }
                }
            }
        }
    }

    threadgroup_barrier(mem_flags::mem_device);

    if (localIndex == 0u) {
        // Slot kSlotCompletedGroups of struct 0 is the shared cross-group
        // completion counter in both modes.
        const uint finished = atomic_fetch_add_explicit(
            &indirectParams[kSlotCompletedGroups],
            1u,
            memory_order_relaxed
        ) + 1u;
        const uint totalGroups = groupCount.x * groupCount.y;
        if (finished == totalGroups) {
            if (frameData.perAxisRoute == 0) {
                writeDispatchDims(
                    indirectParams, 0u,
                    frameData.voxelRenderOptions.x, frameData.voxelRenderOptions.y
                );
            } else {
                for (int axis = 0; axis < 3; ++axis) {
                    writeDispatchDims(
                        indirectParams, uint(axis) * kPerAxisIndirectStrideUints,
                        frameData.voxelRenderOptions.x, frameData.voxelRenderOptions.y
                    );
                }
            }
        }
    }
}
