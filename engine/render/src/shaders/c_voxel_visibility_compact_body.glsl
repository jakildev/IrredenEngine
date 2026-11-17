// Voxel visibility compact — the shared body of two compile-time
// specializations. The wrappers define IR_FOG_CEILING_PASS and include this
// body after ir_iso_common.glsl / ir_constants.glsl / ir_voxel_dispatch.glsl:
//   c_voxel_visibility_compact.glsl          IR_FOG_CEILING_PASS 0
//   c_voxel_visibility_compact_ceiling.glsl  IR_FOG_CEILING_PASS 1
// VOXEL_TO_TRIXEL_STAGE_1 dispatches the ceiling kernel only for a canvas
// whose bound fog window has a ceiling-enabled source live; every other
// dispatch takes the 0 kernel, which carries no ceiling code (the ceiling
// branch's register footprint taxes every rotating frame when it is merely
// present). Metal twin: metal/c_voxel_visibility_compact_body.metal.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Per-axis store list-walk split, carried in the perAxisRoute_ slot (which the compact
    // does not otherwise read). 0 = single full list. Non-zero = split mode: append each
    // visible voxel into the axis regions it has an exposed face on; the value is the
    // per-region element capacity, i.e. the stride between those three regions in
    // compactedVoxelIndices.
    uniform int perAxisSplitStride;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    // Prefix through residualYaw is the shared binding-7 head. The remaining fields
    // match the FrameDataVoxelToCanvas / stage-1 UBO offsets. This pass reads faceDeform and
    // occlusionCullMipCount (per-voxel occlusion cull) and visibleIsoBounds,
    // isDetachedCanvas and feederSubCap (feeder classify); the rest are layout placeholders.
    uniform float isDetachedCanvas;     // offset 76
    uniform vec4 faceDeform[3];         // offset 80
    uniform ivec4 visibleFaceIds;       // offset 128
    uniform vec4 voxelDepthAxis;        // offset 144
    uniform vec4 detachedWorldReceive;  // offset 160
    // Un-widened (no shadow-feeder sweep) visible iso viewport. The feeder classify routes a
    // survivor OUTSIDE this box (an off-screen feeder, stage 2's skip convention) to the
    // feeder dispatch struct instead of the full-density visible list. The per-voxel
    // occlusion cull does not gate on this box; its domain is the whole Hi-Z canvas.
    uniform ivec4 visibleIsoBounds;     // offset 176
    uniform int resolveMode;            // offset 192
    // Per-voxel Hi-Z occlusion-cull gate: 0 = off, non-zero = Hi-Z chain level count → run
    // the per-voxel test.
    uniform int occlusionCullMipCount;  // offset 196
    // Shadow-feeder dispatch partition. feederSubCap = the per-face-edge micro-grid cap for
    // the feeder dispatch (struct 1's zTotal = feederSubCap²); feederPassTailBase is read by
    // stage 1.
    uniform int feederSubCap;           // offset 200
    uniform int feederPassTailBase;     // offset 204
    ivec4 overflowScratchLayout;
    ivec4 overflowSortStep;
    vec4 detachedViewToWorld;
    ivec4 perAxisStoreFrame;
    ivec4 fogCeilingEnabled;
    vec4 cameraViewRotation;
};

// Finest Hi-Z downsampled level (conceptual mip 1) over this frame's complete canvas distances, R32I.
// Bound as a read-only image at unit 1 by VOXEL_TO_TRIXEL_STAGE_1 when this compact runs.
// Read only when occlusionCullMipCount > 0; off unit 0 so it never aliases the fog image
// there.
layout(r32i, binding = 1) readonly uniform iimage2D hiZLevel0;

// Strict-behind margin (one raw-depth unit of encoded slack) so FMA / round
// noise on the boundary never culls a voxel only coplanar with the occluder,
// and so the encoded low bits (slot [1:0] + flip [2]) never tip the
// comparison. Tracks kDepthEncodeShift, matching kOcclusionDepthMargin in
// c_chunk_occlusion_cull.glsl (both = the encode scale).
const int kOcclusionDepthMargin = kDepthEncodeShift;

layout(std430, binding = 5) readonly buffer PositionBuffer {
    vec4 positions[];
};

// Per-voxel material/flag/bone word. The face-occlusion flags byte (bits [2..7] of
// byte 5 = `materialFlagBone >> 8`) drives the fully-interior drop and routes each
// visible voxel into the axis regions it has an exposed face on in split mode.
// Layout must match C_Voxel and the `Voxel` struct in c_voxel_to_trixel_stage_1_body.glsl
// (12 B). Binding 6 (VoxelColorBuffer) is bound every frame, so this is safe to declare
// unconditionally.
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
// `m_voxelColors[i].color_.alpha_ != 0` at frame-upload time.
layout(std430, binding = 8) readonly buffer VoxelActiveMaskBuffer {
    uint activeMask[];
};

layout(std430, binding = 24) readonly buffer ChunkVisibility {
    uint chunkVisible[];
};

layout(std430, binding = 25) writeonly buffer CompactedIndices {
    uint compactedVoxelIndices[];
};

// Indirect dispatch params, declared flat so one kernel writes either layout. Structs sit
// kPerAxisIndirectStrideUints apart (256 B) so the CPU can bindRange each at an
// SSBO-offset-aligned boundary:
//   single-canvas mode  -> IndirectDispatchParams: struct 0 = the visible list,
//                          struct 1 = the shadow-feeder list
//   per-axis split mode  -> PerAxisIndirectDispatchParams: one struct per axis
// Per-struct slots: 0 = numGroupsX, 1 = numGroupsY, 2 = numGroupsZ,
//                   3 = visibleCount (atomic append counter), 4 = completedGroups.
// Slot 4 of struct 0 (params[4]) is the shared cross-group completion counter in
// BOTH modes.
const uint kPerAxisIndirectStrideUints = 64u;  // 256 B / 4 — mirrors C++ kPerAxisSsboAlignBytes
layout(std430, binding = 26) buffer IndirectDispatchParamsBuf {
    uint params[];
};

// Fog-of-war column cull. The world fog canvas binds its camera-anchored fog window texture
// here; every other canvas (detached, GUI, non-fog creations) binds a 1×1 all-visible
// placeholder. A voxel whose RAW world (x,y) column is unexplored is dropped from BOTH the
// single-list and the per-axis appends, so it never rasterizes and FOG_TO_TRIXEL has no pixel
// of it to hard-black. The `imageSize().x <= 1` short-circuit makes the placeholder path a true
// no-op.
layout(rg32ui, binding = 0) readonly uniform uimage2D canvasFogOfWar;

// Mirrors C_CanvasFogOfWar + c_fog_to_trixel.glsl. The fog `.r` lane is the
// state integer; decoded through fogTexelState (unexplored 0.0, explored
// ≈0.5, visible 1.0) `state < kFogExploredThreshold` selects ONLY unexplored
// columns. Explored columns still rasterize (FOG_TO_TRIXEL desaturates them
// as "memory"). Radial admission is channel-blind; the ceiling envelope
// uses the exact cell channels.
const float kFogExploredThreshold = 0.25;

// Live analytic fog vision circles (aliases binding 27; uploaded by VOXEL_TO_TRIXEL_STAGE_1
// before this compact). Std140-mirrors FrameDataFogObservers (C_CanvasFogOfWar) and the
// FogObserverData UBO in c_fog_to_trixel.glsl. canvasFogOfWar carries only coarse
// explored/voxelized memory; these discs carry the smooth "currently visible". The compact
// keeps a column covered by any disc EVEN when its grid cell is unexplored, so a voxel-floor
// scene driven purely by setVisionCircle keeps its floor. The tail after the count is the
// line-of-sight mask (unread here) and the field column at texel (0, 0) of the fog window.
layout(std140, binding = 27) uniform FogObserverData {
    vec4 visionCircles[kMaxFogVisionCircles]; // (centerX, centerY, radius, edgeSoftness)
    int visionCircleCount;
    int losSourceMask;
    int windowOriginX;
    int windowOriginY;
    // The no-ceiling specialization declares only this prefix: a std140 block
    // truncated after a field keeps every earlier offset, and Metal sizes its
    // constant preload from the declared type, so the tail stays out of the
    // kernel that never reads it.
#if IR_FOG_CEILING_PASS
    vec4 visionCircleHeights[kMaxFogVisionCircles];
    vec4 unexploredColor;
    vec4 losParams[kMaxFogVisionCircles];
    uvec4 visionCircleChannels[2];
    vec4 visionCircleCeilings[kMaxFogVisionCircles];
    vec4 revealSurfaceTreatment;
#endif
};

#if IR_FOG_CEILING_PASS
uvec2 fogSelectionCell(ivec2 column) {
    const ivec2 texel = fogWindowTexel(column, ivec2(windowOriginX, windowOriginY), imageSize(canvasFogOfWar));
    return texel.x < 0 ? uvec2(0u, kFogChannelDefault) : imageLoad(canvasFogOfWar, texel).rg;
}
#endif

// True iff this voxel's raw world column is unexplored on the bound fog
// texture. Uses voxelPosRaw (the pre-cardinal-rotation world position) because
// the fog grid is world-space. The 1×1 placeholder returns false (visible → no
// cull); a column outside the window reads unexplored (true), matching every
// other tap, so only a live circle keeps it.
bool fogColumnUnexplored(ivec3 voxelPosRaw) {
    ivec2 fogSize = imageSize(canvasFogOfWar);
    if (fogSize.x <= 1) {
        return false;
    }
    ivec2 fogCell = fogWindowTexel(voxelPosRaw.xy, ivec2(windowOriginX, windowOriginY), fogSize);
    if (fogCell.x < 0) {
        return true;
    }
    return fogTexelState(imageLoad(canvasFogOfWar, fogCell).r) < kFogExploredThreshold;
}

// All six face-occlusion bits of the voxel flags byte (bits [2..7], the
// shader-side mirror of IRComponents::VoxelFlags::kFaceOccludedMask). A
// flags byte matching the full mask marks a fully-interior voxel.
const uint kFaceOccludedMaskBits = 0xFCu;
const uint kFogWholeBodyExemptBit = 1u << 3u;

// Safety margin (cells): covers the per-pixel worldPerPixel AA that
// c_fog_to_trixel adds at low zoom — this shader can't compute zoom —
// PLUS stage 1's fog-hidden keep ring (kFogHiddenKeepCells in
// ir_voxel_face_select.{glsl,metal}) that the image-space
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
    uint voxelsPerGroup = voxelDispatchVoxelsPerGroup(microSliceCount);
    uint groups = (count + voxelsPerGroup - 1u) / voxelsPerGroup;
    uint gx = max(min(groups, 1024u), 1u);
    params[base + 0u] = gx;
    params[base + 1u] = max((groups + gx - 1u) / gx, 1u);
    params[base + 2u] = (microSliceCount + uint(kStageMicroSlicesPerGroup) - 1u) /
        uint(kStageMicroSlicesPerGroup);
}

// Per-voxel Hi-Z occlusion refine, layered on top of the per-chunk pre-pass
// (coarse -> fine hierarchical occlusion). Drops a voxel that is
// locally-exposed + in-frustum but globally occluded by closer geometry, which
// the chunk pass may retain. Subdivided modes skip the chunk pass.
// Test every texel of a conservative emission hull against the current
// frame's full depth pass. Keep partial coverage, background and equal-depth
// contenders; only strictly hidden voxels leave the election/color list.
// Stage 1 already ran for every candidate, so depth, AO and legacy shadow
// feeders remain intact. A zero gate preserves the first (unculled) compact.
bool voxelOccludedByHiZ(ivec3 voxelPos, ivec2 isoPos, vec3 sourcePosition) {
    if (occlusionCullMipCount <= 0) return false;
    const int subdivisions = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    if (voxelRenderOptions.x != 0) {
        voxelPos = roundHalfUp(snapNearIntegerVoxelPosition(sourcePosition) * float(subdivisions));
        voxelPos = rotateCardinalZ(voxelPos, rasterYawCardinalIndex(rasterYaw));
        isoPos = pos3DtoPos2DIso(voxelPos);
    }
    const ivec2 base =
        trixelFrameOffset(trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions) + isoPos;
    const vec2 hullCorners[4] =
        vec2[4](vec2(0.0, 0.0), vec2(2.0, 0.0), vec2(0.0, 3.0), vec2(2.0, 3.0));
    vec2 loF = vec2(1.0e30);
    vec2 hiF = vec2(-1.0e30);
    for (int face = 0; face < 3; ++face) {
        const mat2 deform = mat2(faceDeform[face].xy, faceDeform[face].zw);
        for (int corner = 0; corner < 4; ++corner) {
            const vec2 offset = deform * hullCorners[corner];
            loF = min(loF, offset);
            hiF = max(hiF, offset);
        }
    }
    // Every micro-cell of all six faces lies in the positive subdivision
    // cube. Its iso projection includes both polarities and feeder samples.
    // The cube origin is also a lower bound on every emitted raw depth.
    if (voxelRenderOptions.x != 0) {
        const vec2 cubeReach = vec2(subdivisions, 2 * subdivisions);
        loF -= cubeReach;
        hiF += cubeReach;
    }
    // Include rounding at the emitted hull; a partial footprint is never clamped.
    const ivec2 loTexel = (base + ivec2(floor(loF)) - ivec2(1)) >> 1;
    const ivec2 hiTexel = (base + ivec2(ceil(hiF)) + ivec2(1)) >> 1;
    const ivec2 size = imageSize(hiZLevel0);
    if (any(lessThan(loTexel, ivec2(0))) || any(greaterThanEqual(hiTexel, size))) return false;
    int farthest = -2147483648;
    for (int y = loTexel.y; y <= hiTexel.y; ++y) {
        for (int x = loTexel.x; x <= hiTexel.x; ++x) {
            farthest = max(farthest, imageLoad(hiZLevel0, ivec2(x, y)).x);
        }
    }
    return encodeDepthWithFace(pos3DtoDistance(voxelPos), 0) > farthest + kOcclusionDepthMargin;
}

shared uint groupUniqueSurvivors[64];

void finalizeDispatchDims() {
    const uint visibleSlices = uint(voxelDispatchMicroSliceCount(
        voxelRenderOptions.x, voxelRenderOptions.y, 0
    ));
    if (perAxisSplitStride == 0) {
        writeDispatchDims(0u, visibleSlices);
        // Struct 1 = the feeder dispatch, strided to
        // feederSubCap² micro-cells per face (vs effSub² for visible).
        // Empty when no survivor was classified feeder (shadows off / all
        // on-screen) ⇒ its stage-1 dispatch early-returns every workgroup.
        const uint feederSlices = uint(voxelDispatchMicroSliceCount(
            voxelRenderOptions.x, feederSubCap, 0
        ));
        writeDispatchDims(kPerAxisIndirectStrideUints, feederSlices);
    } else {
        // Unrolled, not a loop over axis 0..2: NVIDIA's link-time
        // optimizer dies with "C5025 lvalue in array access too
        // complex" + a C9999 ICE when a loop-variant base mixes
        // with the constant-base call sites in the single-list branch;
        // constant bases at every call site keep the inlined stores
        // foldable.
        // Face-local stores carry one sample; the scatter reconstructs
        // their footprint independently of presentation subdivisions.
        writeDispatchDims(0u, 1u);
        writeDispatchDims(kPerAxisIndirectStrideUints, 1u);
        writeDispatchDims(2u * kPerAxisIndirectStrideUints, 1u);
    }
}

void main() {
    groupUniqueSurvivors[gl_LocalInvocationIndex] = 0u;
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
                // Cardinal-rotated position, shared with the per-voxel occlusion
                // test. Rotated only on the residual==0 path (the only path where
                // occlusionCullMipCount can be non-zero); stays raw on the
                // rotating path, which never runs the test.
                ivec3 voxelPos = voxelPosRaw;
                // Smooth camera Z-yaw: while the per-axis canvases
                // are active (residual yaw != 0) the framebuffer scatter
                // rasterizes each voxel at its CONTINUOUS yawed iso position, so
                // the cull must project the same way — the cardinal-snapped iso
                // disagrees by the residual and drops off-center voxels. Widen by
                // the deformed-face sqrt2 footprint (~2 iso px) so a voxel whose
                // center is just off-screen but whose face reaches on-screen
                // still rasterizes.
                if (spatialViewEnabled(cameraViewRotation)) {
                    isoPos = roundHalfUp(spatialViewIso(vec3(voxelPosRaw) - kVoxelRasterCellAnchor, cameraViewRotation));
                    cullMargin = 3;
                } else if (residualYaw != 0.0) {
                    // Cell-anchor projection so the cull tests the
                    // same screen position the scatter renders at.
                    isoPos = roundHalfUp(
                        pos3DtoPos2DIsoYawedCellAnchor(vec3(voxelPosRaw), visualYaw));
                    cullMargin = 2;
                } else {
                    if (cardinalIndex != 0) {
                        // Plain cardinal rotation (no lower-corner shift);
                        // mirrors the stage-1/2 store cell.
                        voxelPos = rotateCardinalZ(voxelPos, cardinalIndex);
                    }
                    isoPos = pos3DtoPos2DIso(voxelPos);
                }
                bool fogWholeBodyExempt =
                    (voxels[idx].reserved & kFogWholeBodyExemptBit) != 0u;

#if IR_FOG_CEILING_PASS
                // The CPU dispatches this kernel only while a ceiling-enabled
                // source is live on the bound fog window; the 1×1 placeholder
                // (a detached re-voxelize canvas keyed on its model column) keeps
                // the ceiling gate off here, and stage 1 alone cuts that route.
                const bool ceilingActive = !fogWholeBodyExempt && imageSize(canvasFogOfWar).x > 1;
                const float ceilingSpan = revealSurfaceTreatment.x != 0.0
                    ? kFogCeilingFlickerSpan : 0.0;
                const uint admitted = ceilingActive ? fogCeilingSourceMask(
                    voxelPosRaw.xy, fogSelectionCell(voxelPosRaw.xy), visionCircles,
                    visionCircleChannels, visionCircleCount) : 0u;
                // Upper envelope: every phase's ceiling height is at least
                // dzUp - span, so a voxel with no presence here is retained at
                // no phase and never reaches stage 1.
                const bool ceilingPossible = !ceilingActive || fogCeilingPresenceFromMask(
                    voxelPosRaw.z, admitted, visionCircleHeights,
                    visionCircleCeilings, visionCircleCount, -ceilingSpan) > 0.0;
#else
                const bool ceilingPossible = true;
#endif
                if (isoPos.x >= cullIsoMin.x - cullMargin &&
                    isoPos.x <= cullIsoMax.x + cullMargin &&
                    isoPos.y >= cullIsoMin.y - cullMargin &&
                    isoPos.y <= cullIsoMax.y + cullMargin &&
                    (fogWholeBodyExempt || !fogColumnUnexplored(voxelPosRaw) ||
                     fogColumnInVisionCircle(voxelPosRaw)) && ceilingPossible) {
                    if (perAxisSplitStride == 0) {
                        // Fully-interior drop: a voxel with all six face-occlusion
                        // bits set can emit nothing downstream — stage 1/2 fail
                        // faceIsExposed on every slot, and the opposite-polarity
                        // rotated emit needs an exposed opposite face — so
                        // skipping its append is output-identical and saves its
                        // sub²-slice stage-1 dispatch. Exception: an active fog
                        // vision circle can revive an occluded vertical face as
                        // a cross-section cut wall, so the drop is gated
                        // off while any circle is live. No early return here or
                        // in the Hi-Z refine: every invocation must reach the
                        // cross-group completion barrier.
                        uint flagsByte = (voxels[idx].materialFlagBone >> 8u) & 0xFFu;
                        if ((visionCircleCount != 0 ||
                             (flagsByte & kFaceOccludedMaskBits) != kFaceOccludedMaskBits) &&
                            !voxelOccludedByHiZ(voxelPos, isoPos, positions[idx].xyz)) {
                            // Split this survivor into the visible
                            // list (struct 0, full effSub² density) or the
                            // off-screen shadow-feeder list (struct 1, strided
                            // feederSubCap² density). isShadowFeederIso
                            // (ir_iso_common.glsl) is the one definition shared
                            // with stage 2's skip: a cardinal (residualYaw == 0)
                            // world (isDetachedCanvas < 0.5) survivor whose iso is
                            // outside the un-widened visible viewport. `isoPos` is
                            // the cardinal-snapped iso in that branch
                            // (residualYaw != 0 short-circuits isFeeder to false),
                            // so a voxel this shader calls feeder is exactly one
                            // stage 2 skips — over-classifying VISIBLE is the only
                            // failure mode and never corrupts on-screen output.
                            // Feeders are off-screen by construction, so their
                            // coarser trixel depth reaches only the sun-shadow
                            // bake, never a visible pixel.
                            bool isFeeder = isShadowFeederIso(
                                isoPos, visibleIsoBounds, residualYaw, isDetachedCanvas
                            );
                            if (isFeeder) {
                                // Tail-append: feeder slot i lands at
                                // voxelCount-1-i, growing down from the top of the
                                // compacted buffer so it never collides with the
                                // visible forward append (nVisible + nFeeder ≤
                                // survivors ≤ voxelCount). Struct 1's count slot is
                                // the feeder's in single-list mode (per-axis owns it in
                                // split mode; the two are mutually exclusive here).
                                uint slot = atomicAdd(params[kPerAxisIndirectStrideUints + 3u], 1u);
                                compactedVoxelIndices[uint(voxelCount) - 1u - slot] = idx;
                            } else {
                                uint slot = atomicAdd(params[3], 1u);
                                compactedVoxelIndices[slot] = idx;
                            }
                        }
                    } else {
                        // Per-axis split: append this voxel into each axis
                        // region whose axis it has an exposed face on. The store
                        // shader re-checks the precise visible-face exposure per
                        // axis, so an over-inclusive entry here is harmless; a
                        // fully-interior voxel also enters any region adjacent to a possible
                        // ceiling cut, bounded by the lower visibility envelope.
                        uint flagsByte = (voxels[idx].materialFlagBone >> 8u) & 0xFFu;
                        uint stride = uint(perAxisSplitStride);
#if IR_FOG_CEILING_PASS
                        // Lower envelope of the voxel's own column (every phase's
                        // ceiling height is at most dzUp + span): below 1 means the
                        // voxel or a neighbour sharing its column can be rejected.
                        const float ownMinimum = ceilingActive ? fogCeilingPresenceFromMask(
                            voxelPosRaw.z, admitted, visionCircleHeights,
                            visionCircleCeilings, visionCircleCount, ceilingSpan) : 1.0;
#endif
                        for (int axis = 0; axis < 3; ++axis) {
#if IR_FOG_CEILING_PASS
                            bool possibleCut = false;
                            if (ceilingActive) {
                                ivec3 step = ivec3(0);
                                step[axis] = 1;
                                const ivec3 plus = voxelPosRaw + step;
                                const ivec3 minus = voxelPosRaw - step;
                                const float plusMinimum = axis == 2
                                    ? fogCeilingPresenceFromMask(plus.z, admitted, visionCircleHeights,
                                        visionCircleCeilings, visionCircleCount, ceilingSpan)
                                    : fogCeilingPresence(plus, fogSelectionCell(plus.xy), visionCircles, visionCircleHeights, visionCircleCeilings, visionCircleChannels, visionCircleCount, ceilingSpan);
                                const float minusMinimum = axis == 2
                                    ? fogCeilingPresenceFromMask(minus.z, admitted, visionCircleHeights,
                                        visionCircleCeilings, visionCircleCount, ceilingSpan)
                                    : fogCeilingPresence(minus, fogSelectionCell(minus.xy), visionCircles, visionCircleHeights, visionCircleCeilings, visionCircleChannels, visionCircleCount, ceilingSpan);
                                possibleCut = ownMinimum < 1.0 || plusMinimum < 1.0 || minusMinimum < 1.0;
                            }
#else
                            const bool possibleCut = false;
#endif
                            if (faceIsExposed(flagsByte, 2 * axis) ||
                                faceIsExposed(flagsByte, 2 * axis + 1) || possibleCut) {
                                groupUniqueSurvivors[gl_LocalInvocationIndex] = 1u;
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
        if (perAxisSplitStride != 0) {
            uint uniqueSurvivors = 0u;
            for (int lane = 0; lane < 64; ++lane) {
                uniqueSurvivors += groupUniqueSurvivors[lane];
            }
            atomicAdd(params[5], uniqueSurvivors);
        }
        // params[4] (struct 0's completedGroups slot) is the shared cross-group
        // completion counter in both modes.
        uint finished = atomicAdd(params[4], 1u) + 1u;
        uint totalGroups = gl_NumWorkGroups.x * gl_NumWorkGroups.y;
        if (finished == totalGroups) {
            finalizeDispatchDims();
        }
    }
}
