#version 450 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_constants.glsl"

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Per-axis store list-walk split (#1739): the per-region element capacity
    // (the perAxisRoute_ slot, dead during the compact). 0 = single full list
    // (byte-identical). Non-zero = split mode: append each visible voxel into the
    // axis regions it has an exposed face on; the value is the stride between
    // those three regions in compactedVoxelIndices.
    uniform int perAxisSplitStride;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    // Prefix through residualYaw is the shared binding-7 head. The fields below
    // (matching FrameDataVoxelToCanvas / the stage-1 UBO offsets) are declared so
    // the per-voxel occlusion cull (#1812) can read visibleIsoBounds (offset 176)
    // and occlusionCullMipCount (offset 196), and the #2258 Step-B feeder
    // classify (with isDetachedCanvas + residualYaw) can read the feeder lanes
    // (offsets 200/204); the rest are layout placeholders this pass does
    // not consume.
    uniform float isDetachedCanvas;     // offset 76 (was _yawPadding)
    uniform vec4 faceDeform[3];         // offset 80
    uniform ivec4 visibleFaceIds;       // offset 128
    uniform vec4 voxelDepthAxis;        // offset 144
    uniform vec4 detachedWorldReceive;  // offset 160
    // Un-widened (no shadow-feeder sweep) visible iso viewport (#1740).
    // Consumer here: the #2258 Step-B classify routes a survivor OUTSIDE this
    // box (an off-screen feeder, the exact stage-2 #1740 skip convention) to
    // the feeder dispatch struct instead of the full-density visible list. The
    // per-voxel occlusion cull no longer gates on this box as of #2298 — it now
    // tests the full shadow-feeder-widened canvas via a Hi-Z canvas-COVERAGE
    // guard (see voxelOccludedByHiZ); visibleIsoBounds must stay for the classify.
    uniform ivec4 visibleIsoBounds;     // offset 176
    uniform int resolveMode;            // offset 192
    // Per-voxel Hi-Z occlusion-cull gate (#1812): 0 = off (byte-identical),
    // non-zero = Hi-Z chain level count → run the per-voxel test.
    uniform int occlusionCullMipCount;  // offset 196
    // Offsets 200/204 — #2258 Step-B feeder partition (shifted one slot down
    // by the #1812 gate). feederSubCap = the per-face-edge micro-grid cap for
    // the feeder dispatch (struct 1's zTotal = feederSubCap²);
    // feederPassTailBase is read by stage 1. The former feederPass flag at 208
    // is a compile-time IR_FEEDER_PASS specialization now (architect a′), so
    // it's a reserved pad — not declared here.
    uniform int feederSubCap;           // offset 200
    uniform int feederPassTailBase;     // offset 204
};

// Finest Hi-Z downsampled level (conceptual mip 1) over last frame's canvas
// distances (#1798), R32I. Bound as a read-only IMAGE at unit 1 (not a sampler)
// by VOXEL_TO_TRIXEL_STAGE_1 when this compact runs. The image bind is load-
// bearing on Metal: bindComputeResources flushes the image-binding table AFTER
// the sampler table at the same encoder texture index, and the image table is
// sticky, so the leftover trixelDistances IMAGE bound at unit 1 by the prior
// frame's stage-1/stage-2 shadowed a sampler bind of the Hi-Z here — the compact
// then read freshly-cleared trixelDistances (the all-65535 distance sentinel)
// instead of the Hi-Z, and the per-voxel test never fired (#1812 zero-capture).
// Binding the Hi-Z as an image overwrites that stale slot so it wins the flush.
// Read only when occlusionCullMipCount > 0; off unit 0 so it never aliases the
// fog image there.
layout(r32i, binding = 1) readonly uniform iimage2D hiZLevel0;

// Strict-behind margin (one raw-depth unit of encoded slack) so FMA / round
// noise on the boundary never culls a voxel only coplanar with the occluder,
// and so the encoded low bits (slot [1:0] + flip [2], #2207) never tip the
// comparison. Tracks kDepthEncodeShift, matching kOcclusionDepthMargin in
// c_chunk_occlusion_cull.glsl (both = the encode scale).
const int kOcclusionDepthMargin = kDepthEncodeShift;

layout(std430, binding = 5) readonly buffer PositionBuffer {
    vec4 positions[];
};

// Per-voxel material/flag/bone word. Read ONLY in per-axis split mode
// (perAxisSplitStride != 0): the face-occlusion flags byte (bits [2..7] of
// byte 5 = `materialFlagBone >> 8`) routes each visible voxel into the axis
// regions it has an exposed face on. Layout must match C_Voxel and the `Voxel`
// struct in c_voxel_to_trixel_stage_1.glsl (12 B). Binding 6 (VoxelColorBuffer)
// is bound every frame, so this is safe to declare unconditionally.
struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};
layout(std430, binding = 6) readonly buffer ColorBuffer {
    Voxel voxels[];
};

// Per-slot active bitmask uploaded from `C_VoxelPool::m_activeMask`:
// one uint32 per `kVoxelActiveMaskBits` (= 32) voxel slots; bit i mirrors
// `m_voxelColors[i].color_.alpha_ != 0` at frame-upload time. The previous
// path read `voxels[idx].colorPacked` purely to test alpha; this SSBO
// replaces that read with a 1-bit lookup so inactive slots short-circuit
// before touching the wider color SSBO. T-287 / #950.
layout(std430, binding = 8) readonly buffer VoxelActiveMaskBuffer {
    uint activeMask[];
};

layout(std430, binding = 24) readonly buffer ChunkVisibility {
    uint chunkVisible[];
};

layout(std430, binding = 25) writeonly buffer CompactedIndices {
    uint compactedVoxelIndices[];
};

// Indirect dispatch params, declared flat so one kernel writes either layout:
//   single-canvas mode  -> the 32-byte IndirectDispatchParams buffer (struct 0)
//   per-axis split mode  -> PerAxisIndirectDispatchParams: three structs spaced
//                           kPerAxisIndirectStrideUints apart (256 B), so the CPU
//                           can bindRange each at an SSBO-offset-aligned boundary.
// Per-struct slots: 0 = numGroupsX, 1 = numGroupsY, 2 = numGroupsZ,
//                   3 = visibleCount (atomic append counter), 4 = completedGroups.
// Slot 4 of struct 0 (params[4]) is the shared cross-group completion counter in
// BOTH modes.
const uint kPerAxisIndirectStrideUints = 64u;  // 256 B / 4 — mirrors C++ kPerAxisSsboAlignBytes
layout(std430, binding = 26) buffer IndirectDispatchParamsBuf {
    uint params[];
};

// Fog-of-war column cull (#2008). The world fog canvas binds its 256² fog
// visibility texture here; every other canvas (detached, GUI, non-fog
// creations) binds a 1×1 all-visible placeholder. A voxel whose RAW world
// (x,y) column is unexplored is dropped from BOTH the single-list and the
// per-axis appends, so it never rasterizes — there is no surviving pixel for
// FOG_TO_TRIXEL to hard-black, which is what turned a tall object on an
// unrevealed column into a black silhouette. The `imageSize().x <= 1`
// short-circuit makes the placeholder path a true no-op, so non-fog canvases
// stay byte-identical to master.
layout(rgba8, binding = 0) readonly uniform image2D canvasFogOfWar;

// Mirrors C_CanvasFogOfWar + c_fog_to_trixel.glsl. The fog `.r` channel reads
// back in normalized space: unexplored 0.0, explored ≈0.5, visible 1.0 — so
// `state < kFogExploredThreshold` selects ONLY unexplored columns. Explored
// columns still rasterize (FOG_TO_TRIXEL desaturates them as "memory").
const int kFogOfWarHalfExtent = 128;
const float kFogExploredThreshold = 0.25;

// Live analytic fog vision circles (aliases binding 27 — uploaded by
// VOXEL_TO_TRIXEL_STAGE_1 right before this compact). Std140-mirrors
// FrameDataFogObservers (C_CanvasFogOfWar) and the FogObserverData UBO in
// c_fog_to_trixel.glsl. The grid texture above carries only coarse
// explored/voxelized memory; these discs carry the smooth "currently visible".
// The compact keeps a column covered by any disc EVEN when its grid cell is
// unexplored, so a voxel-floor scene driven purely by setVisionCircle keeps its
// floor (the grid-only cull would otherwise drop every column and black it out).
const int kMaxFogVisionCircles = 8; // mirror of component_canvas_fog_of_war.hpp kMaxFogVisionCircles — must stay in sync
layout(std140, binding = 27) uniform FogObserverData {
    vec4 visionCircles[kMaxFogVisionCircles]; // (centerX, centerY, radius, edgeSoftness)
    int visionCircleCount;
};

// True iff this voxel's raw world column is unexplored on the bound fog
// texture. Uses voxelPosRaw (the pre-cardinal-rotation world position) because
// the fog grid is world-space; out-of-range columns and the 1×1 placeholder
// both return false (visible → no cull), matching c_fog_to_trixel's bounds
// convention.
bool fogColumnUnexplored(ivec3 voxelPosRaw) {
    ivec2 fogSize = imageSize(canvasFogOfWar);
    if (fogSize.x <= 1) {
        return false;
    }
    ivec2 fogCell = ivec2(
        voxelPosRaw.x + kFogOfWarHalfExtent,
        voxelPosRaw.y + kFogOfWarHalfExtent
    );
    if (fogCell.x < 0 || fogCell.x >= fogSize.x ||
        fogCell.y < 0 || fogCell.y >= fogSize.y) {
        return false;
    }
    return imageLoad(canvasFogOfWar, fogCell).r < kFogExploredThreshold;
}

// All six face-occlusion bits of the voxel flags byte (bits [2..7], the
// shader-side mirror of IRComponents::VoxelFlags::kFaceOccludedMask). A
// flags byte matching the full mask marks a fully-interior voxel.
const uint kFaceOccludedMaskBits = 0xFCu;

// Safety margin (cells): covers the per-pixel worldPerPixel AA that
// c_fog_to_trixel adds at low zoom — this shader can't compute zoom —
// PLUS the fog-hidden keep ring (kFogHiddenKeepCells in
// c_voxel_to_trixel_stage_1.{glsl,metal}) that the image-space
// cross-section cut renders from; this margin must stay a superset of
// stage 1's keep (8 + ~0.5 aa) or ring voxels are culled before stage 1
// can keep them.
const float kCullSafetyCells = 9.0;

// True iff this column lies under any live analytic vision circle, so its
// voxels survive the grid cull and FOG_TO_TRIXEL can reveal them smoothly per
// pixel. Uses a cell nearest-point (AABB) test: the voxel cell at integer
// (cx,cy) covers [cx-0.5, cx+0.5]×[cy-0.5, cy+0.5]; clamping the offset to
// that extent and measuring the residual gives the distance from the circle
// center to the nearest point of the cell. Strictly more permissive than a
// center-only test, so columns whose nearest corner touches the smooth band
// rasterize — the per-pixel mask owns the visible edge at pixel resolution
// and no grid-aligned notches appear along the arc.
bool fogColumnInVisionCircle(ivec3 voxelPosRaw) {
    vec2 col = vec2(voxelPosRaw.xy);
    for (int i = 0; i < visionCircleCount; ++i) {
        vec2 d = abs(col - visionCircles[i].xy) - vec2(0.5);
        d = max(d, vec2(0.0));
        float keepR = visionCircles[i].z + max(visionCircles[i].w, 0.0) + kCullSafetyCells;
        if (dot(d, d) <= keepR * keepR) {
            return true;
        }
    }
    return false;
}

// Compute the indirect dispatch grid for the struct at `base` from its
// visibleCount slot (matches the single-canvas numGroups math exactly). The
// count is read atomically so the last group sees every other group's appends.
void writeDispatchDims(uint base, uint microSliceCount) {
    uint count = atomicAdd(params[base + 3u], 0u);
    uint gx = max(min(count, 1024u), 1u);
    params[base + 0u] = gx;
    params[base + 1u] = max((count + gx - 1u) / gx, 1u);
    // #2258: the stage kernels raster `microSliceCount` micro-cells per voxel
    // face. Packing kStageMicroSlicesPerGroup of them into each z-workgroup
    // (local_size_z in the stage kernels) means the launched z-workgroup count
    // is the ceil-divided slice count; the stage re-derives its micro-slice as
    // gl_WorkGroupID.z * kStageMicroSlicesPerGroup + gl_LocalInvocationID.z and
    // early-returns the tail past microSliceCount, so output is byte-identical.
    // Step B's feeder struct passes feederSubCap² here instead of effSub².
    params[base + 2u] = (microSliceCount + uint(kStageMicroSlicesPerGroup) - 1u) /
        uint(kStageMicroSlicesPerGroup);
}

// Per-voxel Hi-Z occlusion refine (#1812), layered on top of the per-chunk
// pre-pass (coarse -> fine hierarchical occlusion). Drops a voxel that is
// locally-exposed + in-frustum but globally occluded by closer geometry —
// capturing the per-voxel occludedExposedFraction the all-or-nothing 256-voxel
// chunk test leaves on the table. Conservative + off by default:
//   * occlusionCullMipCount == 0 (the default; any non-cardinal / rotating /
//     re-voxelize / no-Hi-Z frame) -> no test, byte-identical to master.
//   * Domain = the full shadow-feeder-widened canvas (#2298). The Hi-Z
//     downsample-maxes the WHOLE distance canvas — visible viewport plus the
//     shadow-feeder ring the feeders raster into — so a ring caster's occluder
//     data is real and testable. The gate is a canvas-COVERAGE guard, not a
//     viewport box: a voxel is tested iff its expanded footprint lies fully
//     inside the Hi-Z texel extent (a footprint spilling off-canvas would clamp
//     onto the border texel and is kept — see the guard below). SOUNDNESS: a
//     voxel conservatively occluded at every canvas texel it can raster to leaves
//     no trace in trixelDistances, and BOTH the visible resolve and the
//     sun-shadow bake consume trixelDistances (never voxels) — so dropping it is
//     bit-identical for the shadow it would have cast too. This supersedes the
//     #1812 "never cull a shadow-feeder" mitigation (correct only while the test
//     domain was assumed visible-only).
//   * A footprint that still sees background keeps the voxel (empty texels carry
//     the 65535 sentinel -> hiZMax stays large -> never occlude). A false
//     positive is a visible hole; a false negative is only lost savings.
// Encoding matches the Hi-Z exactly: encodeDepthWithFace(pos3DtoDistance(voxelPos), 0)
// — the same cardinal iso depth dispatchChunkOcclusion writes as its
// encodedNearest_ (cb.minDepth_ * kDepthEncodeShift, face slot 0). Route through
// the shared encode helper, NOT an open-coded scale: #2207 changed the cardinal
// layout from *4 to *kDepthEncodeShift (depth [31:3] | flip [2] | slot [1:0]),
// and a hard-coded *4 here silently compares a half-scale depth against the
// Hi-Z's *8 values so the test never fires (a vacuous no-op). flip = 0 on the
// cardinal path this cull runs on (riser-flip is gated to rotated content), so
// slot 0 / flip 0 is the correct encode; the margin absorbs the Hi-Z's low bits.
bool voxelOccludedByHiZ(ivec3 voxelPos, ivec2 isoPos) {
    if (occlusionCullMipCount <= 0) {
        return false;
    }
    // iso -> canvas pixel, exactly as dispatchChunkOcclusion / stage 1 at NONE
    // mode (effectiveTrixelSubdivisionScale == 1). This is stage 1's emit `base`.
    ivec2 base = trixelCanvasOffsetZ1 + ivec2(floor(frameCanvasOffset)) + isoPos;
    // Sample the Hi-Z over the EMISSION HULL — the conservative superset of every
    // pixel stage 1 can write for this voxel — NOT a fixed ±1 window. stage 1's
    // emitDeformedFace writes each face at `base + roundHalfUp(D_s * src)` for src
    // across the [0,2)x[0,3) invocation lattice (local_size 2x3), per visible-
    // triplet slot s, with D_s = mat2(faceDeform[s].xy, faceDeform[s].zw). The
    // fixed ±1 window missed the +iso extreme of that hull at silhouette / upper
    // edges (the near Z face is unexposed so it never writes at `base`, and the
    // exposed X/Y faces land past the window), false-culling a VISIBLE voxel whose
    // own last-frame depth write fell outside the sampled window — the #1812
    // static-scene hole. The window MUST stay a superset of emitDeformedFace's
    // write set (the self-referential Hi-Z then re-anchors every surviving voxel):
    // KEEP IN SYNC with c_voxel_to_trixel_stage_1.{glsl,metal} emitDeformedFace.
    // faceDeform is identity at residualYaw==0 (the only path the cull runs on), so
    // this reduces to a fixed ~3x4-texel box on the cardinal path; deriving it from
    // faceDeform keeps it correct by construction if the emit deform ever changes.
    const vec2 hullCorners[4] =
        vec2[4](vec2(0.0, 0.0), vec2(2.0, 0.0), vec2(0.0, 3.0), vec2(2.0, 3.0));
    vec2 loF = vec2(1.0e30);
    vec2 hiF = vec2(-1.0e30);
    for (int s = 0; s < 3; ++s) {
        mat2 D = mat2(faceDeform[s].xy, faceDeform[s].zw);
        for (int c = 0; c < 4; ++c) {
            vec2 p = D * hullCorners[c];
            loF = min(loF, p);
            hiF = max(hiF, p);
        }
    }
    // ±1 px pad absorbs roundHalfUp of the intra-hull super-sample taps; >>1 maps
    // canvas px to the finest half-res Hi-Z level.
    ivec2 loTexel = (base + ivec2(floor(loF)) - ivec2(1)) >> 1;
    ivec2 hiTexel = (base + ivec2(ceil(hiF)) + ivec2(1)) >> 1;
    ivec2 sz = imageSize(hiZLevel0);
    // Canvas-coverage guard (#2298): only cull a voxel whose full expanded
    // footprint lies inside the Hi-Z texel extent [0, sz). c_build_distance_hiz
    // ceil-sizes each level and writes every texel a real downsampled max
    // (background sentinel 65535 where empty), so every read in [0, sz-1] is
    // faithful. A footprint that spills PAST that extent has no data off-canvas;
    // the former clamp folded such a tap onto the border texel — reading a near
    // occluder in place of the empty-background sentinel, deflating hiZMax and
    // risking a false cull of an edge voxel that actually sees background. Keep
    // those voxels. Voxels fully inside the old visibleIsoBounds gate are a
    // strict subset of this domain (the canvas >= the visible viewport), so their
    // test — and the cull-off byte-identity — is unchanged; the widening only
    // ADDS the shadow-feeder ring, this issue's target population. With the guard
    // holding, the read is provably in-bounds, so the former per-tap clamp is dead.
    if (loTexel.x < 0 || loTexel.y < 0 || hiTexel.x >= sz.x || hiTexel.y >= sz.y) {
        return false;
    }
    int hiZMax = -2147483648;
    for (int ty = loTexel.y; ty <= hiTexel.y; ++ty) {
        for (int tx = loTexel.x; tx <= hiTexel.x; ++tx) {
            hiZMax = max(hiZMax, imageLoad(hiZLevel0, ivec2(tx, ty)).x);
        }
    }
    return encodeDepthWithFace(pos3DtoDistance(voxelPos), 0) > hiZMax + kOcclusionDepthMargin;
}

void main() {
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    uint workGroupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    uint idx = workGroupIndex * 64u + gl_LocalInvocationID.x;

    if (idx < uint(voxelCount)) {
        uint chunkIdx = idx / uint(VOXEL_CHUNK_SIZE);
        if (chunkVisible[chunkIdx] != 0u) {
            // 32-bit word stride matches `kVoxelActiveMaskBits` on the CPU side.
            uint maskWord = activeMask[idx >> 5u];
            if (((maskWord >> (idx & 31u)) & 1u) != 0u) {
                // roundHalfUp keeps tie cells consistent with the stage-1/2
                // raster (hardware round() ties are implementation-defined).
                ivec3 voxelPosRaw = roundHalfUp(positions[idx].xyz);
                ivec2 isoPos;
                int cullMargin = 0;
                // Cardinal-rotated position, hoisted so the per-voxel occlusion
                // test can reuse it. Only rotated in the residual==0 branch below
                // (the only path where occlusionCullMipCount can be non-zero);
                // stays raw on the rotating path, which never runs the test.
                ivec3 voxelPos = voxelPosRaw;
                // Smooth camera Z-yaw (T3 / #1310): while the per-axis canvases
                // are active (residual yaw != 0) the framebuffer scatter
                // rasterizes each voxel at its CONTINUOUS yawed iso position, so
                // the cull must project the same way — the cardinal-snapped iso
                // disagrees by the residual and drops off-center voxels (the
                // "missing objects during rotation" symptom). Widen by the
                // deformed-face sqrt2 footprint (~2 iso px) so a voxel whose
                // center is just off-screen but whose face reaches on-screen
                // still rasterizes. residual == 0 keeps the byte-identical
                // cardinal-snap path.
                if (residualYaw != 0.0) {
                    isoPos = roundHalfUp(pos3DtoPos2DIsoYawed(vec3(voxelPosRaw), visualYaw));
                    cullMargin = 2;
                } else {
                    if (cardinalIndex != 0) {
                        voxelPos = rotateCardinalZ(voxelPos, cardinalIndex);
                        voxelPos += cardinalLowerCornerShift(cardinalIndex);
                    }
                    isoPos = pos3DtoPos2DIso(voxelPos);
                }
                if (isoPos.x >= cullIsoMin.x - cullMargin &&
                    isoPos.x <= cullIsoMax.x + cullMargin &&
                    isoPos.y >= cullIsoMin.y - cullMargin &&
                    isoPos.y <= cullIsoMax.y + cullMargin &&
                    (!fogColumnUnexplored(voxelPosRaw) ||
                     fogColumnInVisionCircle(voxelPosRaw))) {
                    if (perAxisSplitStride == 0) {
                        // Fully-interior drop: a voxel with all six face-occlusion
                        // bits set can emit nothing downstream — stage 1/2 fail
                        // faceIsExposed on every slot, and the opposite-polarity
                        // rotated emit needs an exposed opposite face — so
                        // skipping its append is output-identical and saves its
                        // sub²-slice stage-1 dispatch. Exception: an active fog
                        // vision circle can revive an occluded vertical face as
                        // a cross-section cut wall (#2125), so the drop is gated
                        // off while any circle is live. (No early return — the
                        // cross-group completion barrier below must stay
                        // uniformly reached.)
                        // Per-voxel Hi-Z occlusion refine (#1812): drop a voxel
                        // globally occluded by closer geometry. Off by default
                        // (occlusionCullMipCount == 0 -> no-op), and skipped for a
                        // fully-interior voxel that was already dropped above, so
                        // it never re-adds one. No early return — the completion
                        // barrier below must stay uniformly reached.
                        uint flagsByte = (voxels[idx].materialFlagBone >> 8u) & 0xFFu;
                        if ((visionCircleCount != 0 ||
                             (flagsByte & kFaceOccludedMaskBits) != kFaceOccludedMaskBits) &&
                            !voxelOccludedByHiZ(voxelPos, isoPos)) {
                            // #2258 Step B: split this survivor into the visible
                            // list (struct 0, full effSub² density) or the
                            // off-screen shadow-feeder list (struct 1, strided
                            // feederSubCap² density). The feeder test is the EXACT
                            // stage-2 #1740 skip: a cardinal (residualYaw == 0)
                            // world (isDetachedCanvas < 0.5) survivor whose iso is
                            // outside the un-widened visible viewport. `isoPos` is
                            // already the cardinal-snapped iso in that branch
                            // (residualYaw != 0 short-circuits isFeeder to false),
                            // so a voxel this shader calls feeder is exactly one
                            // stage 2 skips — over-classifying VISIBLE is the only
                            // failure mode and never corrupts on-screen output.
                            // Feeders are off-screen by construction, so their
                            // coarser trixel depth reaches only the sun-shadow
                            // bake, never a visible pixel.
                            bool isFeeder =
                                residualYaw == 0.0 && isDetachedCanvas < 0.5 &&
                                (isoPos.x < visibleIsoBounds.x || isoPos.x > visibleIsoBounds.z ||
                                 isoPos.y < visibleIsoBounds.y || isoPos.y > visibleIsoBounds.w);
                            if (isFeeder) {
                                // Tail-append: feeder slot i lands at
                                // voxelCount-1-i, growing down from the top of the
                                // compacted buffer so it never collides with the
                                // visible forward append (nVisible + nFeeder ≤
                                // survivors ≤ voxelCount). Struct 1's count slot is
                                // dead in single-list mode (per-axis owns it in
                                // split mode; the two are mutually exclusive here).
                                uint slot = atomicAdd(params[kPerAxisIndirectStrideUints + 3u], 1u);
                                compactedVoxelIndices[uint(voxelCount) - 1u - slot] = idx;
                            } else {
                                uint slot = atomicAdd(params[3], 1u);
                                compactedVoxelIndices[slot] = idx;
                            }
                        }
                    } else {
                        // Per-axis split (#1739): append this voxel into each axis
                        // region whose axis it has an exposed face on. The store
                        // shader re-checks the precise visible-face exposure per
                        // axis, so an over-inclusive entry here is harmless; a
                        // fully-interior voxel (every face occluded) lands in no
                        // region, which is tighter than master's full-list walk.
                        uint flagsByte = (voxels[idx].materialFlagBone >> 8u) & 0xFFu;
                        uint stride = uint(perAxisSplitStride);
                        for (int axis = 0; axis < 3; ++axis) {
                            if (faceIsExposed(flagsByte, 2 * axis) ||
                                faceIsExposed(flagsByte, 2 * axis + 1)) {
                                uint base = uint(axis) * kPerAxisIndirectStrideUints;
                                uint slot = atomicAdd(params[base + 3u], 1u);
                                compactedVoxelIndices[uint(axis) * stride + slot] = idx;
                            }
                        }
                    }
                }
            }
        }
    }

    barrier();
    memoryBarrierBuffer();

    if (gl_LocalInvocationIndex == 0u) {
        // params[4] (struct 0's completedGroups slot) is the shared cross-group
        // completion counter in both modes.
        uint finished = atomicAdd(params[4], 1u) + 1u;
        uint totalGroups = gl_NumWorkGroups.x * gl_NumWorkGroups.y;
        if (finished == totalGroups) {
            int subdivisions = max(voxelRenderOptions.y, 1);
            uint visibleSlices =
                (voxelRenderOptions.x != 0) ? uint(subdivisions * subdivisions) : 1u;
            if (perAxisSplitStride == 0) {
                writeDispatchDims(0u, visibleSlices);
                // #2258 Step B: struct 1 = the feeder dispatch, strided to
                // feederSubCap² micro-cells per face (vs effSub² for visible).
                // Empty when no survivor was classified feeder (shadows off / all
                // on-screen) ⇒ its stage-1 dispatch early-returns every workgroup.
                int cap = max(feederSubCap, 1);
                uint feederSlices = (voxelRenderOptions.x != 0) ? uint(cap * cap) : 1u;
                writeDispatchDims(kPerAxisIndirectStrideUints, feederSlices);
            } else {
                // Unrolled (was a for over axis 0..2): NVIDIA's link-time
                // optimizer dies with "C5025 lvalue in array access too
                // complex" + a C9999 ICE when the loop-variant base mixes
                // with the constant-base call sites above; constant bases
                // at every call site keep the inlined stores foldable.
                writeDispatchDims(0u, visibleSlices);
                writeDispatchDims(kPerAxisIndirectStrideUints, visibleSlices);
                writeDispatchDims(2u * kPerAxisIndirectStrideUints, visibleSlices);
            }
        }
    }
}
