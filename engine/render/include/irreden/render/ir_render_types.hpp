#ifndef IR_RENDER_TYPES_H
#define IR_RENDER_TYPES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/math/sdf.hpp>
#include <irreden/render/lod_level.hpp>

#include <cstdint>

using namespace IRMath;

namespace IRRender {

/// Opaque handle returned by @c createResource<T>() / @c createNamedResource<T>().
/// Pass to @c getResource<T>() / @c destroyResource<T>().
typedef uint32_t ResourceId;

/// Internal type discriminant used by @c RenderingResourceManager; not used by callers.
typedef uint32_t ResourceType;

/// Single trixel (triangle-pixel) value written by the voxel→trixel compute shaders.
struct TrixelData {
    vec4 color_;
    int distance_;
};

/// UBO uploaded once at init time. Contains the distance sentinel values used
/// in both GLSL and Metal shaders to detect "empty" trixels.
struct GlobalConstantsGLSL {
    int kMinTriangleDistance = IRConstants::kTrixelDistanceMinDistance;
    int kMaxTriangleDistance = IRConstants::kTrixelDistanceMaxDistance;
};

struct FrameDataFramebuffer {
    mat4 mvpMatrix;
    vec2 textureOffset; // TODO: Update in texture scroll system and make
    // a frame data component as well or add as field for shader program
};

/// Per-frame UBO for the SPRITE_TO_SCREEN pass. The orthographic projection
/// is computed CPU-side from the current viewport (one mat4 per frame, never
/// per-instance) so the vertex shader's per-sprite work stays minimal.
struct FrameDataSpritesToScreen {
    mat4 projection_;
};

/// Per-instance entry uploaded into the SpriteInstancesBuffer SSBO consumed
/// by the SPRITE_TO_SCREEN vertex shader. Layout is std430-friendly (vec4
/// alignment, 48 bytes total) so the GLSL and MSL declarations can share
/// the same byte layout. Screen position is the sprite quad's top-left
/// corner in screen-pixel coordinates (Y up); size is in screen pixels.
struct GpuSpriteInstance {
    vec4 screenPosSize_; ///< (screenX, screenY, sizeX, sizeY)
    vec4 uvRect_;        ///< (u0, v0, u1, v1), normalized to atlas
    vec4 tintRgba_;      ///< per-sprite tint, components in [0, 1]
};
static_assert(sizeof(GpuSpriteInstance) == 48, "GpuSpriteInstance must remain 48 bytes (std430)");

/// CPU-side intermediate for the SPRITE_TO_SCREEN gather + sort. Holds the
/// data needed to (a) sort by iso depth back-to-front and (b) group runs of
/// equal texture handles for one drawArraysInstanced call per atlas, then
/// project into a GpuSpriteInstance during upload.
struct SpriteRenderEntry {
    ResourceId textureHandle_ = 0;
    int isoDepth_ = 0; ///< pos3DtoDistance(world); larger = farther
    vec2 screenPos_ = vec2(0.0f);
    vec2 size_ = vec2(0.0f);
    vec4 uvRect_ = vec4(0.0f, 0.0f, 1.0f, 1.0f);
    vec4 tintRgba_ = vec4(1.0f);
};

/// CPU mirror of the @c HoveredEntityIdBuffer SSBO layout (binding 14).
/// The fragment shader writes the hovered entity id + depth here every frame;
/// @c IRRender::getEntityIdAtMouseTrixel reads it back via persistent map.
/// The binary layout must stay byte-identical to the GLSL/MSL @c std430 block
/// declared in @c f_trixel_to_framebuffer.glsl and @c trixel_to_framebuffer.metal.
struct HoveredEntityIdLayout {
    uvec2 entityId_{0u, 0u};
    float depth_{1.0f};
    float _pad_{0.0f};
};

struct FrameDataTrixelToFramebuffer {
    mat4 mpMatrix_;
    vec2 canvasZoomLevel_;
    vec2 cameraTrixelOffset_;
    vec2 textureOffset_;
    vec2 mouseHoveredTriangleIndex_;
    /// x = effective subdivisions for smooth-mode hover coord conversion.
    /// y = depth rescale (effSub / canvas renderedSub) applied to rawDist in
    /// f_trixel_to_framebuffer before distanceOffset_ is added, so a world-placed
    /// DETACHED canvas rastered at a capped sub lands in the shared framebuffer
    /// depth units; <= 0 means 1.0 (the world/overlay fast path).
    vec2 effectiveSubdivisionsForHover_;
    /// Config: when 0, hovered trixel is not visually highlighted (entity detection still works).
    float showHoverHighlight_;
    /// Added to (rescaled) raw canvas distance before depth normalization. 0 for
    /// world/overlay canvases; for a world-placed per-entity canvas it is the
    /// entity world iso depth in encoded framebuffer units
    /// (pos3DtoDistance(entityPos) * effSub * 8 — the encodeDepthWithFace ×8 over
    /// the ×subdivision depth scale).
    int distanceOffset_ = 0;
    /// Smooth camera Z-yaw forward-scatter composite (T3 / #1310). Consumed
    /// only by the per-axis scatter shaders (v_/f_peraxis_scatter); the
    /// cardinal-fast-path gather shaders read only the prefix above, so these
    /// are an std140 append (existing field offsets unchanged → byte-identical
    /// fast path preserved). `perAxisBase_` is the canvas-pixel origin of the
    /// per-axis canvas being scattered (= trixelOriginOffsetZ1(axisSize) +
    /// floor(cameraIso * subdivisionScale)); `visibleFaceIds_` mirrors
    /// FrameDataVoxelToCanvas (per-slot world FaceId, .w pad).
    ivec2 perAxisBase_{0, 0};
    float visualYaw_ = 0.0f;
    /// Raw @c IRRender::DebugOverlayMode value for the per-axis scatter's
    /// instrumentation modes (PER_AXIS_ID / PER_AXIS_ORIGIN, #1457); 0 or any
    /// lighting-pass mode renders the normal composite. Occupies the std140
    /// pad slot at offset 124, so existing offsets are unchanged.
    int scatterDebugMode_ = 0;
    ivec4 visibleFaceIds_{0, 0, 0, 0};
    /// Reserved std140 slots at offsets 144 / 160. These formerly carried the
    /// detached-entity SO(3) forward-scatter residual quat + iso depth axis
    /// (P3b / #1475), consumed only by v_peraxis_scatter_detached. That detached
    /// forward-scatter path was retired in #1560 — detached SO(3) now renders
    /// through the re-voxelize path (#1555–#1559) — but the two slots are kept
    /// (never written, never read) so `scatterFbResolution_` stays at the shared
    /// std140 offset 176 the CAMERA scatter shader reads. `v_peraxis_scatter.glsl`
    /// already declares these as `_detachedResidualPad` / `_detachedDepthAxisPad`
    /// padding, so the camera path is byte-identical. Do NOT reorder or drop
    /// these without also reflowing the camera scatter shader's UBO block.
    vec4 detachedResidual_{0.0f, 0.0f, 0.0f, 1.0f};
    vec4 detachedDepthAxis_{1.0f, 1.0f, 1.0f, 0.0f};
    /// Framebuffer resolution (.xy) the scatter renders into; .zw pad. Lets the
    /// per-axis scatter vertex shaders convert a screen-space conservative-
    /// coverage margin (in framebuffer pixels) into clip/NDC so a sub-pixel-thin
    /// deformed face rhombus still covers a fragment center (#1494). std140-
    /// appended after detachedDepthAxis_ so the gather + world/detached scatter
    /// blocks that stop earlier stay byte-identical; only the scatter shaders
    /// that dilate read it.
    vec4 scatterFbResolution_{0.0f, 0.0f, 0.0f, 0.0f};
    /// Depth-color debug mode for the per-axis scatter path (#1697). When
    /// depthColorMode_ != 0, the scatter fragment shader evaluates hue from the
    /// interpolated face-corner world depth (smooth continuous gradient, no moiré)
    /// instead of using the pre-baked per-voxel vColor. Matches the SDF twin's
    /// per-pixel evaluation in c_shapes_to_trixel. depthColorExtent_ is the
    /// bounding half-sum (= dColor in applyDepthColor) used to normalize [0,1].
    /// std140-appended at offset 192 — only the scatter shaders read it; the
    /// gather fast path stays byte-identical.
    int depthColorMode_ = 0;
    float depthColorExtent_ = 0.0f;
    /// No-priority perf fast-path (#2155). 0 = no voxel drawn into this canvas
    /// carries a non-zero per-trixel priority (#1960), so f_trixel_to_framebuffer
    /// skips the per-fragment `triangleEntityIds` decode read on the default path
    /// (still read for hovered fragments so picking is unaffected). != 0 = at
    /// least one priority voxel may be present, so the read runs and the tier is
    /// decoded as before. Conservative-TRUE: a false positive only costs the fast
    /// path, never correctness (a skipped read would decode tier 0 anyway, so the
    /// flag-off output is byte-identical). Stamped per canvas from the pool's
    /// priority-voxel count (C_TriangleCanvasTextures::anyPerTrixelPriority_).
    /// Repurposes the former `_depthColorPad0_` std140 slot (offset 200) — a
    /// 4-byte scalar, so no size/offset change and the scatter UBO asserts below
    /// stay valid.
    int anyPerTrixelPriority_ = 0;
    /// Two-tier composite depth partition (#1958). 0 = WORLD content: the gather
    /// (f_trixel_to_framebuffer) clamps `enc` OUT of the reserved near band
    /// (`enc = max(enc, kDepthForegroundCeil + 1)`) — a no-op for every in-budget
    /// demo, so the byte-identical fast path is preserved. != 0 = FOREGROUND
    /// priority: the gather pins this draw's model-frame local iso-depth INTO the
    /// reserved near band (`clamp(enc, kMin, kDepthForegroundCeil)`), so a floating
    /// solid is unconditionally nearer than any world fragment regardless of world
    /// extent (the disjoint near-plane partition of the #1884 escalation). Only the
    /// detached composite (ENTITY_CANVAS_TO_FRAMEBUFFER) for a world-placed
    /// C_EntityCanvas with depthPriority_ != 0 uploads a non-zero value; the main
    /// gather + every other producer leave it 0. Repurposes the former
    /// `_depthColorPad1_` std140 slot (offset 204) — no size/offset change, so the
    /// scatter UBO asserts below stay valid.
    int depthPriorityMode_ = 0;
    /// View-visibility overflow lane draw selector (#2333). 0 = the normal
    /// per-cell scatter / gather draws (the only value any non-overflow draw
    /// uploads). 1 = the overflow instanced indirect draw drawPerAxisScatter
    /// issues after the three per-axis cell draws: v_peraxis_scatter then pulls
    /// each instance's {iso cell, colorPacked, encoded distance} entry from the
    /// SSBO at binding 25 (the overflow entry region bindRange'd there) instead
    /// of texelFetching a canvas cell. Read only by the per-axis scatter vertex
    /// shaders; std140-appended (offset 208) so every prior offset — and the
    /// gather shaders reading only the prefix — stays unchanged.
    int overflowMode_ = 0;
    int overflowPad0_ = 0;
    int overflowPad1_ = 0;
    int overflowPad2_ = 0;
};
static_assert(
    offsetof(FrameDataTrixelToFramebuffer, visibleFaceIds_) == 128,
    "FrameDataTrixelToFramebuffer::visibleFaceIds_ must land at offset 128 "
    "(distanceOffset_ ends at 112 → perAxisBase_ 112 / visualYaw_ 120 / "
    "scatterDebugMode_ 124, then std140 ivec4 alignment rounds to 128)"
);
static_assert(
    offsetof(FrameDataTrixelToFramebuffer, detachedResidual_) == 144 &&
        offsetof(FrameDataTrixelToFramebuffer, detachedDepthAxis_) == 160 &&
        offsetof(FrameDataTrixelToFramebuffer, scatterFbResolution_) == 176,
    "Scatter UBO tail must std140-append after visibleFaceIds_ (ends at 144): "
    "detachedResidual_ 144 / detachedDepthAxis_ 160 / scatterFbResolution_ 176. "
    "The first two are now reserved padding (detached forward-scatter retired "
    "#1560) holding scatterFbResolution_ at offset 176 for the camera scatter "
    "shader; the gather block stops at visibleFaceIds_, so it stays byte-identical"
);
static_assert(
    offsetof(FrameDataTrixelToFramebuffer, depthColorMode_) == 192,
    "depthColorMode_ must land at offset 192 (scatterFbResolution_ ends at 192); "
    "std140 int is 4-byte aligned, so it follows directly"
);
static_assert(
    offsetof(FrameDataTrixelToFramebuffer, overflowMode_) == 208,
    "overflowMode_ must std140-append after the depthColorMode_ block "
    "(ends at 208) so every prior offset — and the gather shaders reading only "
    "the prefix — stays unchanged"
);
static_assert(
    sizeof(FrameDataTrixelToFramebuffer) == 224,
    "FrameDataTrixelToFramebuffer size must mirror its std140 GLSL block. The "
    "camera scatter shaders (v_/f_peraxis_scatter) read the appended "
    "perAxisBase_ / visualYaw_ / visibleFaceIds_ and the #1494 scatterFbResolution_ "
    "at offset 176; depthColorMode_ / depthColorExtent_ at offset 192 carry the "
    "per-pixel depth-color mode for the scatter path (#1697); a silent reorder "
    "or resize would corrupt the scatter UBO with no compile diagnostic"
);

/// Multiplier in the @c encodeDepthWithFace convention (d·8 + flip·4 + face,
/// #2207 silhouette-riser polarity carrier at bit 2), shared by the
/// world-placed detached-canvas composite and any producer that converts world
/// iso depth or model-frame rawDist into shared framebuffer depth units
/// (×effSub × 8). Mirror of @c kDepthEncodeShift in
/// `ir_iso_common.glsl` / `metal/ir_iso_common.metal`.
constexpr int kDepthEncodeShift = 8;

/// Two-tier composite depth partition (#1958; the #1884 sub-epic's Bug-A fix).
/// The most-negative @c kDepthForegroundBandWidth codes of the shared
/// `[kTrixelDistanceMinDistance, kTrixelDistanceMaxDistance]` depth range are
/// RESERVED for foreground-priority detached solids. A fragment whose composite
/// `enc` lands in that band is unconditionally nearer (GL_LESS +
/// `normalizeDistance`) than any non-priority "world" fragment, INDEPENDENT of
/// world extent — dominance is by partition membership, not by out-sizing the
/// world iso-depth spread. This is the disjoint near-plane partition the #1884
/// escalation settled on, retiring the additive priority band (no fixed band
/// could dominate unbounded world placement — the radius-200 GRID orbit proved
/// it). World content is clamped to stay OUT of the band; the clamp is a no-op
/// for every current demo at the effSub-16 cap (it fires only when
/// `cardinalIsoDepth·8 < kDepthForegroundCeil` ≈ world extent far past
/// canvas_stress's r=200 orbit), so the cardinal fast path and all in-budget
/// content stay byte-identical. Mirror in `ir_iso_common.glsl` /
/// `metal/ir_iso_common.metal`. Full model:
/// docs/design/depth-unification-1884-investigation.md §Resolution.
constexpr int kDepthForegroundBandWidth = 16384;
/// Inclusive far edge of the reserved foreground band:
/// `[kTrixelDistanceMinDistance, kDepthForegroundCeil]`. World content is clamped
/// to `>= kDepthForegroundCeil + 1`.
constexpr int kDepthForegroundCeil =
    IRConstants::kTrixelDistanceMinDistance + kDepthForegroundBandWidth;
/// Center of the reserved band. The composite sets `distanceOffset_` to this for
/// a priority entity so its OWN model-frame local iso-depth lands centered in the
/// band and self-occludes correctly; the gather clamps to the band edges so a
/// pathologically deep solid saturates (graceful degradation) instead of escaping.
constexpr int kDepthForegroundBandCenter =
    IRConstants::kTrixelDistanceMinDistance + kDepthForegroundBandWidth / 2;

// Partition-layout asserts (#1958). These REPLACE the plan's original
// world-extent band assert (`2·4·maxSubdividedIso + 3 < BAND`), which the
// architect escalation proved unsatisfiable — world placement is unbounded, so
// no sound compile-time `maxSubdividedIso` exists. The partition needs only that
// the reserved band is on the near side and lies inside the encodable range.
static_assert(
    kDepthForegroundCeil < 0,
    "reserved foreground band must be on the near (most-negative) side of the range"
);
static_assert(
    IRConstants::kTrixelDistanceMinDistance <= kDepthForegroundCeil &&
        kDepthForegroundCeil < IRConstants::kTrixelDistanceMaxDistance,
    "foreground band [kMin, kDepthForegroundCeil] must lie inside the encodable depth range"
);

// ── Per-trixel priority tiers (#1960) ─────────────────────────────────────────
// Generalizes #1958's two-tier partition (world vs ONE foreground band) into N
// disjoint foreground tiers, selected per fragment by `tier = max(perEntityTier,
// perTrixelTier)` at the depth-finalization chokepoint (f_trixel_to_framebuffer).
// More-negative `enc` = nearer (GL_LESS), so a higher tier is a more-negative
// disjoint sub-range of the reserved band — unconditionally in front of every
// lower tier AND of all world content, independent of world extent. Default tier
// 0 everywhere ⇒ byte-identical to #1958 master (the world clamp is unchanged and
// every producer that authors no priority stays at tier 0).
//
// N = 3: tier 0 = world (clamped OUT of the band), tier 1 = entity-foreground
// (the #1958 `C_EntityCanvas::depthPriority_` path), tier 2 = per-trixel override
// (a voxel-authored priority that renders in front even of an entity-foreground
// solid — Demo 2's "animate inside each other"). Architect ruling (#1884/#1958
// steward direction) caps the carrier at K=2 stolen id bits ⇒ at most 4 tiers;
// raise N only up to that ceiling, else switch to a dedicated attachment.
constexpr int kDepthForegroundTierCount = 3;
// Equal-width split of the reserved band across the N-1 foreground tiers. Each
// tier holds a unit's subdivided local iso-depth spread at the effSub-16 cap; a
// pathologically deep unit saturates against its tier edge (graceful degradation
// — it stays pinned in front, loses only intra-tier depth resolution), exactly as
// #1958's single band did.
constexpr int kDepthForegroundTierWidth =
    kDepthForegroundBandWidth / (kDepthForegroundTierCount - 1);

// Disjoint sub-range of the reserved band owned by foreground @p tier (1..N-1).
// MORE-negative = higher priority, so tier N-1 sits at the near (most-negative)
// band edge and tier 1 just inside kDepthForegroundCeil; tier 0 (world) has no
// sub-range (it is the out-of-band partition). Mirror: ir_iso_common.{glsl,metal}.
constexpr int depthForegroundTierLo(int tier) {
    return IRConstants::kTrixelDistanceMinDistance +
           (kDepthForegroundTierCount - 1 - tier) * kDepthForegroundTierWidth;
}
constexpr int depthForegroundTierHi(int tier) {
    return depthForegroundTierLo(tier) + kDepthForegroundTierWidth - 1;
}
// Center a priority fragment's model-frame local iso-depth lands on so it
// self-occludes correctly within its tier (per-tier kDepthForegroundBandCenter).
constexpr int depthForegroundTierCenter(int tier) {
    return depthForegroundTierLo(tier) + kDepthForegroundTierWidth / 2;
}

static_assert(kDepthForegroundTierCount >= 2, "need world tier 0 + at least one foreground tier");
static_assert(kDepthForegroundTierWidth > 0, "foreground tier width must be positive");
static_assert(
    (kDepthForegroundTierCount - 1) * kDepthForegroundTierWidth <= kDepthForegroundBandWidth,
    "foreground tiers must fit disjointly inside the reserved band"
);
static_assert(
    depthForegroundTierLo(kDepthForegroundTierCount - 1) == IRConstants::kTrixelDistanceMinDistance,
    "highest-priority tier must start at the near (most-negative) band edge"
);
static_assert(
    depthForegroundTierHi(1) <= kDepthForegroundCeil,
    "lowest-priority foreground tier must stay within the reserved band"
);

// ── Per-trixel priority carrier (#1960) ───────────────────────────────────────
// The per-trixel tier travels voxel authoring → trixel raster → finalization in
// the top K=2 bits of the 64-bit entity id stored in the `triangleEntityIds`
// channel (the only per-texel channel that already reaches finalization). Entity
// ids are allocation counters that never approach 2^62, so the top 2 bits are
// free; a SINGLE decode chokepoint (these helpers + the .glsl/.metal twins) masks
// them off everywhere an id is READ so a non-zero priority can never corrupt a
// picked id (the architect's "neutralize the trap" discipline — no reader
// open-codes the mask). Default priority 0 ⇒ the stored id is unchanged.
constexpr int kEntityIdPriorityBits = 2;
// Shift WITHIN the high 32-bit word (the id's bits 62..63 = the high word's bits
// 30..31). The two 32-bit words are stored as a uvec2 (low = .x, high = .y).
constexpr int kEntityIdPriorityShiftInHighWord = 30;
constexpr std::uint32_t kEntityIdPriorityMaskInHighWord = ((1u << kEntityIdPriorityBits) - 1u)
                                                          << kEntityIdPriorityShiftInHighWord;
// Fog cut-face carrier (#2124 lit-cross-section follow-up): bit 29 of the high
// word, just below the priority tier. Set by c_voxel_to_trixel_stage_2 on a fog
// cross-section CUT face; read by c_lighting_to_trixel to force it fully lit.
// Folded into kEntityIdHighWordMask so the SAME decode chokepoint that strips the
// priority tier also strips this — picking never sees it. Mirror of the
// .glsl/.metal kEntityIdCutFaceMaskInHighWord.
constexpr std::uint32_t kEntityIdCutFaceMaskInHighWord = 1u << 29;
constexpr std::uint32_t kEntityIdHighWordMask =
    ~(kEntityIdPriorityMaskInHighWord | kEntityIdCutFaceMaskInHighWord);
// Full-64-bit shift of the carrier — the invariant a live id must satisfy
// (`id >> kEntityIdPriorityShift == 0`), guarded at the voxel-pool upload boundary.
constexpr int kEntityIdPriorityShift = 32 + kEntityIdPriorityShiftInHighWord;
static_assert(
    kDepthForegroundTierCount <= (1 << kEntityIdPriorityBits),
    "tier count must fit in the K stolen entity-id bits"
);

// Reconstruct the 64-bit entity id from its two stored words with the priority
// carrier bits stripped — THE chokepoint every CPU id reader routes through
// (getEntityIdAtMouseTrixel, C_TriangleCanvasTextures::readEntityIdAt). Mirror of
// the .glsl/.metal `decodeEntityId`.
inline std::uint64_t decodeCarrierEntityId(uvec2 packed) {
    return static_cast<std::uint64_t>(packed.x) |
           (static_cast<std::uint64_t>(packed.y & kEntityIdHighWordMask) << 32);
}
inline std::uint32_t decodeCarrierPriority(uvec2 packed) {
    return (packed.y >> kEntityIdPriorityShiftInHighWord) & ((1u << kEntityIdPriorityBits) - 1u);
}

struct FrameDataVoxelToCanvas {
    vec2 cameraTrixelOffset_;
    ivec2 trixelCanvasOffsetZ1_;
    ivec2 voxelRenderOptions_;
    ivec2 voxelDispatchGrid_;
    int voxelCount_;
    // Doubles as the smooth-camera-Z-yaw per-axis route selector (T2 / #1309;
    // docs/design/per-axis-trixel-canvas-rotation.md). 0 = the normal single-
    // canvas raster (byte-identical to master — this is the only value the
    // single-canvas dispatch ever uploads). 1/2/3 = the per-axis dispatch for
    // the X/Y/Z axis canvas: the voxel→trixel shaders then route ONLY this
    // axis's visible face, reposition its center continuously via
    // pos3DtoPos2DIsoYawed (no cardinal snap), and write the shared world-space
    // depth. Reusing the otherwise-dead std140 padding int keeps the UBO layout
    // (and the size/offset asserts below) unchanged.
    int perAxisRoute_ = 0;
    ivec2 canvasSizePixels_;
    // Iso-space cull viewport: voxels whose iso position falls outside
    // [cullIsoMin, cullIsoMax] are skipped.  Matches the CPU chunk mask
    // viewport so the per-voxel test refines chunk boundaries cleanly.
    // Derived from the cull camera (frozen or live) and zoom.
    ivec2 cullIsoMin_ = ivec2(0);
    ivec2 cullIsoMax_ = ivec2(0);
    // Z-yaw camera rotation, in radians. visualYaw_ is the canonical
    // continuous angle written by gameplay; rasterYaw_ is the cardinal-snap
    // multiple of pi/2 nearest visualYaw_; residualYaw_ = visualYaw_ -
    // rasterYaw_. The integer trixel rasterizer picks a basis permutation
    // from rasterYaw_; the trixel emit shader applies faceDeform_[face] to
    // its sub-pixel offset in 2D iso space to recover the continuous yaw
    // geometrically (T-293, replaces the screen-space bilinear residual
    // composite from T-058 / T-322).
    float visualYaw_ = 0.0f;
    float rasterYaw_ = 0.0f;
    float residualYaw_ = 0.0f;
    // 1.0 for a detached entity canvas; 0.0 for the world canvas. Used by
    // the voxel emit shaders to gate super-sampling (emitDeformedFace n > 1)
    // to the detached path only, keeping the world canvas on T-293's single-tap
    // behavior and preserving its perf baseline. Occupies the std140 padding
    // slot after residualYaw_ so no struct padding changes.
    float isDetachedCanvas_ = 0.0f;
    // Per-slot residual-yaw (or SO(3) for DETACHED) deformation packed
    // column-major: .xy = col0, .zw = col1 of `IRMath::faceDeformationMatrix(
    // axis(visibleFaceIds_[slot]), residualYaw_)`. Identity (col0=(1,0),
    // col1=(0,1)) when residualYaw_ == 0. **Indexed by visible-triplet SLOT
    // (0/1/2)**, not by axis — at non-zero cardinal the world face whose
    // matrix lives at slot s changes per `visibleFaceIds_[s]`. std140 vec4
    // array stride is 16 B so this is 48 B; mirrored as `vec4 faceDeform[3]`
    // in the GLSL/Metal UBO declarations. (#1278 generalization of the
    // T-293 per-axis upload — at cardinal 0 the per-slot matrices match
    // the per-axis ones bit-for-bit so the yaw=0 path stays unchanged.)
    vec4 faceDeform_[3] = {
        vec4(1.0f, 0.0f, 0.0f, 1.0f), vec4(1.0f, 0.0f, 0.0f, 1.0f), vec4(1.0f, 0.0f, 0.0f, 1.0f)
    };
    // Per-slot world `FaceId` (0..5 = X_NEG / X_POS / Y_NEG / Y_POS /
    // Z_NEG / Z_POS) — the three camera-visible faces resolved from the
    // camera quaternion via `IRMath::visibleFaceTripletCardinal` (#1278).
    // Indexed by visible-triplet slot 0/1/2; `.w` (std140 ivec3 rounds up to
    // ivec4 stride anyway) doubles as the detached re-voxelize coverage marker
    // (#1557 Option B): 0 = an ordinary canvas (world, identity, single-canvas /
    // per-axis detached) that emits the exact face footprint; non-zero = a
    // DETACHED_REVOXELIZE canvas whose voxels carry the rotation in their CELL
    // positions and raster at cardinal 0. Such a canvas KEEPS the visible-triplet
    // × exposed-mask gate (SYSTEM_REBUILD_DETACHED_VOXELS recomputes the mask on
    // the rotated cells each frame, so it is correct), and the marker instead
    // tells `c_voxel_to_trixel_stage_{1,2}` to apply conservative-coverage
    // dilation — each surface face grown ±1px along its in-plane iso axes — that
    // closes the sub-cell gaps round-to-cell leaves between adjacent rotated
    // cells. `.w` was formerly written by VOXEL_TO_TRIXEL_STAGE_1 as the
    // MAIN_CANVAS_SO3 canvas flag; that write was removed in #1443 when
    // MAIN_CANVAS_SO3 was retired. AO / lighting only ever index slots 0..2 (the
    // `& 3` neighbour decode is guarded behind a non-empty, per-axis test, and
    // re-voxelize is not a per-axis route), so the marker is invisible to them.
    // Defaults to {X_NEG, Y_NEG, Z_NEG, 0} = the historical lower-coordinate-faces
    // set, so a UBO that hasn't been populated by the new path renders identically
    // to pre-#1278 master at cardinal 0.
    //
    // Consumed by raster (`c_voxel_to_trixel_stage_{1,2}` — exposed-mask
    // check + per-face micro-position) AND by `c_compute_voxel_ao` /
    // `c_lighting_to_trixel` (decoded slot → world FaceId → six-face
    // outward normal / tangents). Single source of face metadata per
    // design-doc § "AO / lighting agree by construction".
    ivec4 visibleFaceIds_ = ivec4(0, 2, 4, 0);
    // Model-frame iso depth axis `R⁻¹·(1,1,1)` for the per-voxel occlusion
    // metric (#1462). The world canvas and any identity entity keep the
    // default (1,1,1) so `isoDepthAlongAxis` collapses to `x+y+z` and the
    // GRID / identity raster stays byte-identical; a rotated DETACHED canvas
    // uploads `IRMath::isoDepthAxisModel(rotation)` so off-octahedral pitch /
    // roll orders voxels along the entity-rotated axis instead of the snapped
    // (1,1,1). `.w` is std140 padding. std140-appended after visibleFaceIds_
    // (offset 144), so every prior field offset — and the world/GRID fast
    // path — is unchanged; the gather shaders that read only the prefix
    // (AO, lighting) are unaffected and need no declaration update.
    vec4 voxelDepthAxis_ = vec4(1.0f, 1.0f, 1.0f, 0.0f);
    // World-receive offset for a world-placed detached re-voxelize solid
    // (#1576 P4b-2; world placement is the default since #1624). `.xyz` = the
    // entity's world cell origin
    // (`roundVec3HalfUp(C_WorldTransform::translation_)`, the SAME rounding the
    // composite depth offset uses); `.w` = 1.0 when the solid is world-placed
    // (`C_EntityCanvas::screenLocked_` false, the default), else 0.0. The detached
    // re-voxelize canvas rasters its pool in the pool-centered MODEL frame, so the
    // lighting / sun-shadow passes recover each voxel's WORLD pos as
    // `modelPos + .xyz` and then sample the SHARED world sun-shadow map + 128³
    // light volume there (receive), equivalently to an attached GRID solid. `.w`
    // gates the whole path: 0.0 keeps the screen-locked overlay opt-out
    // byte-identical (no shadow received, light volume disabled). Default
    // (0,0,0,0) so the world canvas + any screen-locked canvas is unchanged.
    // std140-appended after voxelDepthAxis_ (offset 160), so every prior field
    // offset is unchanged and shaders that read only the prefix need no update.
    // Declared by c_lighting_to_trixel (world-space receive) and by
    // c_voxel_to_trixel_stage_{1,2} (#2127 — recovering a world-placed detached
    // re-voxelize voxel's world column for the fog cut-face cross-section).
    vec4 detachedWorldReceive_ = vec4(0.0f, 0.0f, 0.0f, 0.0f);
    // Un-widened (no shadow-feeder sweep) iso cull viewport for the depth-only
    // shadow-feeder path (#1740). `.xy` = floor(min), `.zw` = ceil(max) of the
    // SAME viewport `cullIsoMin_/cullIsoMax_` derive from, but BEFORE
    // IRMath::shadowFeederIsoBounds widens it toward the sun. A voxel whose
    // cardinal iso position lies inside [cullIsoMin_, cullIsoMax_] (it passed
    // the compact cull) but OUTSIDE this box is an off-screen shadow FEEDER —
    // it only casts sun shadows via the stage-1 distance bake and is never
    // displayed / lit / picked, so c_voxel_to_trixel_stage_2 skips its
    // colour + entity-id taps (stage 1 still writes its full-resolution depth,
    // which is all the bake + AO read). When sun shadows are off the sweep is
    // zero, so this equals cullIsoMin_/cullIsoMax_ and stage 2 skips nothing —
    // byte-identical. Read only by c_voxel_to_trixel_stage_2; std140-appended
    // after detachedWorldReceive_ (offset 176) so every prior offset is
    // unchanged and the gather shaders that read only the prefix need no update.
    ivec4 visibleIsoBounds_ = ivec4(0, 0, 0, 0);
    // Per-axis deterministic-winner resolve mode (#2255). 0 = the normal
    // distance store (the only value any non-per-axis dispatch uploads). 1 =
    // the winner-resolve dispatch dispatchPerAxisCanvases inserts between
    // stage 1 and stage 2 on each axis canvas: stage 1 re-runs the identical
    // per-axis geometry and, for each face whose encoded distance ties the
    // settled per-cell atomicMin winner, atomicMins the face's run-stable
    // voxel pool index into the per-cell winner scratch
    // (kBufferIndex_PerAxisResolveScratch). Stage 2's per-axis color tap then
    // admits exactly one of the tied faces (the minimum index), so the
    // color/entity-id planes are byte-identical run-to-run at a fixed pose —
    // matching the distance plane, whose atomicMin was always
    // order-independent. Read only by c_voxel_to_trixel_stage_1;
    // std140-appended after visibleIsoBounds_ (offset 192) so every prior
    // offset is unchanged. Tail lanes: offset 196 is the #1812 per-voxel
    // occlusion-cull gate; 200/204 are the #2258 Step-B shadow-feeder
    // dispatch partition (repurposed from the former resolveMode pads, shifted
    // one slot down by the #1812 gate), packing the 192..208 16-byte std140
    // stride exactly for the full-struct subData uploads.
    int resolveMode_ = 0;
    // Per-voxel Hi-Z occlusion-cull gate (#1812), refining the per-chunk cull
    // (#1294 child 2/3). 0 = the compact's per-voxel test is skipped, so the
    // default (occlusion cull off) pipeline is byte-identical. Non-zero = the
    // Hi-Z chain level count; the compact samples level 0 at each surviving
    // voxel's canvas pixel and drops it when strictly behind the farthest visible
    // surface. Set (in VOXEL_TO_TRIXEL_STAGE_1) only on the same states the chunk
    // pre-pass is verified for: enabled, lag source fresh, NONE render mode,
    // cardinal (not rotating), non-re-voxelize pool, Hi-Z chain built. Keeps the
    // first resolveMode tail pad slot (offset 196) it shipped on.
    int occlusionCullMipCount_ = 0;
    // Shadow-feeder dispatch partition (#2258 Step B). The compact classifies
    // each surviving voxel as visible (inside visibleIsoBounds_) or off-screen
    // shadow feeder (outside it, the exact stage-2 #1740 skip convention) and
    // tail-appends feeders into a SECOND indirect struct; stage 1 rasters that
    // struct in a second dispatch at a strided micro-grid capped to
    // feederSubCap_ per face edge, so feeder trixelDistances (bake-only, never
    // on-screen) cost feederSubCap² micro-cells instead of the full effSub².
    // feederSubCap_ tracks the sun-bake texel density (system_voxel_to_trixel
    // beginTick); == effSub disarms the reduction (byte-identical), and sun
    // shadows off ⇒ zero feeders ⇒ the whole partition is structurally inert.
    int feederSubCap_ = 0;
    // effectiveVoxelCount the compact was dispatched with — the tail base the
    // feeder pass reads from (feeder slot i lives at feederPassTailBase_-1-i).
    // Lands at offset 204, packing the 192..208 std140 row exactly (no tail
    // pad): the runtime feederPass_ flag that once followed moved to a
    // COMPILE-TIME shader specialization (IR_FEEDER_PASS; architect option
    // a′) — the two stage-1 programs are distinct compiled kernels, so no
    // per-dispatch flag upload is needed and the hottest kernel carries none
    // of the feeder branches.
    int feederPassTailBase_ = 0;
    // View-visibility overflow lane scratch layout (#2333). Region base offsets
    // (in uints) into the unified per-axis resolve scratch bound at
    // kBufferIndex_PerAxisResolveScratch, plus the overflow entry cap:
    // .x = view-mask base, .y = ctrl base (draw args + counters), .z = overflow
    // entry base, .w = entry cap. The scratch's region 0 is the #2255 winner-id
    // array, so the existing perAxisWinnerIds[cell] indexing is unchanged.
    // Read only by c_voxel_to_trixel_stage_1 at resolveMode 2/3 (rotating
    // frames); std140-appended after the #2258 feeder-partition block (offset
    // 208) so every prior offset — and every prefix-reading shader — is unchanged.
    ivec4 overflowScratchLayout_ = ivec4(0, 0, 0, 0);
};

struct FrameDataTrixelToTrixel {
    ivec2 cameraTrixelOffset_;
    ivec2 trixelCanvasOffsetZ1_;
    ivec2 trixelTextureOffsetZ1_;
    vec2 texturePos2DIso_;
};

struct GlyphDrawCommand {
    uint32_t positionPacked; // x | (y << 16)
    uint32_t glyphIndex;
    uint32_t colorPacked; // RGBA as packed uint
    uint32_t distance;
    uint32_t styleFlags = 0;
};

/// How the output canvas is scaled to fill the window.
enum class FitMode { FIT, STRETCH, UNKNOWN };

/// Controls how the voxel→trixel compute pass subdivides trixel cells.
/// - @c NONE          — no subdivision; voxels snap to the nearest trixel
///   grid cell. Fast, pixel-perfect. Equivalent to the old SNAPPED mode.
/// - @c POSITION_ONLY — positions are subdivided by @c subdivisions (no zoom
///   scaling), but SDF/shape evaluation stays at base resolution. Gives
///   smooth entity movement without the GPU cost of full zoom subdivision.
/// - @c FULL          — positions subdivided by @c subdivisions × zoom.
///   Smoothest camera panning but highest GPU cost. Equivalent to the old
///   SMOOTH mode. Changing mode or subdivisions mid-frame stalls the pipeline.
/// @note Currently global (per-frame). Per-entity subdivision modes are future work.
enum class SubdivisionMode { NONE = 0, POSITION_ONLY = 1, FULL = 2 };

/// Where camera Z-yaw rotation pivots.
/// - @c ORIGIN        — yaw rotates content about the fixed world origin. A
///   panned-off-origin camera swings the scene in an arc. Deterministic; the
///   pre-#1352 behavior, kept selectable for demos that rely on it.
/// - @c CAMERA_CENTER — yaw rotates content about the world point under screen
///   center (the camera focus), so panning then rotating spins the scene in
///   place. The correction collapses to the identity at `yaw == 0`, so the
///   cardinal fast path stays byte-identical to @c ORIGIN. Engine default.
enum class RotationPivotMode { ORIGIN = 0, CAMERA_CENTER = 1 };

/// Sentinel `entityTransformIndex` marking a voxel as CPU-direct (static):
/// the GPU voxel-position prepass skips it, leaving its binding-5 slot exactly
/// as the CPU pending-range flush wrote it. Every voxel defaults to this, so a
/// scene with no GPU-transformed voxels is byte-identical to the pre-prepass path.
/// The prepass reads the per-voxel slot from the `.w` lane of its local-position
/// SSBO (binding `kBufferIndex_LocalVoxelPositions`, bit-cast to uint) rather
/// than a dedicated buffer — Metal caps buffer indices at 30 and the engine
/// already fills 0..30, so packing the slot into the otherwise-unused padding
/// lane avoids spending a scarce binding point.
constexpr std::uint32_t kVoxelTransformStatic = 0xFFFFFFFFu;
/// Cap on concurrently GPU-transformed voxel sets (= EntityTransformBuffer slots).
/// A set whose `gpuTransformSlot_` meets or exceeds this stays on the CPU path.
/// Matches the `GpuVoxelTransform` array size allocated by `UPDATE_VOXEL_POSITIONS_GPU`.
constexpr int kMaxGpuVoxelTransforms = 4096;

/// Skeletal joint transforms (#605 Phase 2.2 / #1603) share the same binding-18
/// `EntityTransformBuffer`, NOT a second buffer — the #1396 prepass that consumes
/// per-voxel transform slots is the same operation a per-bone skin matrix needs.
/// The 4096-slot budget is partitioned so the two allocators can't collide and
/// the prepass's contiguous `[0, maxSlotUsed_]` re-upload can never clobber a
/// joint slot: dynamic voxel-set slots grow UP from 0 and stop at
/// `kJointTransformSlotBase`; `UPDATE_JOINT_MATRICES` carves contiguous joint
/// blocks DOWN from `kMaxGpuVoxelTransforms`. The reserved high region holds
/// `kMaxGpuJointTransforms` slots — e.g. ~34 thirty-joint skeletons, or 1024
/// single-joint rigs — leaving the low region for voxel-set transforms.
constexpr int kMaxGpuJointTransforms = 1024;
/// First slot of the reserved joint region; voxel-set transforms use `[0, this)`.
constexpr int kJointTransformSlotBase = kMaxGpuVoxelTransforms - kMaxGpuJointTransforms;

/// One per GPU-transformed voxel-set, indexed by `C_VoxelSetNew::gpuTransformSlot_`
/// (and by each owned voxel's per-voxel transform index). The compute prepass
/// computes `world = modelToWorld_ * vec4(localPos, 1)`, so this carries the full
/// SO(3)+translation (built CPU-side via `IRMath::sqtToMat4`). Column-major
/// `mat4` matches the GLSL `mat4` / Metal `float4x4` layout byte-for-byte (64 B).
/// Generic by design: an entity transform today, a bone transform for skeletal
/// voxels (#605) tomorrow — the prepass never assumes which.
struct GpuVoxelTransform {
    mat4 modelToWorld_ = mat4(1.0f);
};

struct GPUUpdateParams {
    int voxelCount_ = 0;
    int padding_[3] = {};
};

/// Per-frame params for the detached re-voxelize GPU scatter compute
/// (`c_revoxelize_detached`, #1556 + #1619). Carries the detached canvas's
/// composed rotation quaternion (the ONLY per-frame upload — O(entities), the
/// whole point of the GPU path over P1's CPU re-rasterize) and the dispatch
/// domain descriptor. Quaternion layout matches `C_LocalTransform`/`IRMath` —
/// `vec4(qx, qy, qz, qw)`, identity `(0,0,0,1)`.
///
/// Two dispatch modes (`dest_.w`):
///   0 — IDENTITY / source path (#1556, byte-identical to pre-#1619). One thread
///       per LIVE SOURCE voxel; the thread writes `position[slot]` from its
///       resident composed local. The CPU still uploads color + active for these
///       source-indexed slots, so only binding 5 is authored here.
///   1 — INVERSE-RESAMPLE path (#1619). One thread per DEST cell of the
///       rotated-AABB cube (`dest_.y³` cells, center `dest_.z`). Each thread
///       inverse-maps its dest cell `roundHalfUp(R⁻¹·c)` into the per-pool source
///       occupancy+color grid (`srcGridMin_`/`srcGridDims_`); on a hit it authors
///       `position`/`color`/`active` for that dest slot. Surjective over the dest
///       lattice → hole-free (the forward scatter was not). The CPU skips the
///       color + active uploads in this mode (the GPU fill owns them).
///
/// std140 UBO at `kBufferIndex_RevoxelizeDetachedParams`: five 16 B vectors, 80 B.
struct RevoxelizeDetachedParams {
    vec4 canvasRotation_ = vec4(0.0f, 0.0f, 0.0f, 1.0f);
    // x = dispatch count (dest-cell count D in mode 1, live source count in mode 0)
    // y = dest cube side (cells per axis); z = dest cube center; w = inverse mode (0/1)
    ivec4 dest_ = ivec4(0, 0, 0, 0);
    ivec4 srcGridMin_ = ivec4(0, 0, 0, 0);  // xyz = source occupancy grid min cell
    ivec4 srcGridDims_ = ivec4(0, 0, 0, 0); // xyz = source occupancy grid dims
    // xyz = the solid's per-axis half-cell anchor: composed local minus its
    // roundHalfUp cell (-0.5 on even-sized centered axes, 0 on odd). The inverse
    // resample maps between LATTICE cells; the solid's true points sit at
    // cell + anchor, and ignoring that shifted the rotated raster by a constant
    // half cell per even axis (#2349). w unused.
    vec4 anchor_ = vec4(0.0f, 0.0f, 0.0f, 0.0f);
};
static_assert(
    sizeof(RevoxelizeDetachedParams) == 80,
    "RevoxelizeDetachedParams must mirror its std140 GLSL/Metal UBO block: five "
    "16 B vec4/ivec4 = 80 B. A silent reorder or resize would corrupt the "
    "re-voxelize fill's dispatch descriptor with no compile diagnostic."
);

/// SDF primitive type dispatched to the shapes→trixel compute shader.
/// Canonical definition lives in @ref IRMath::SDF::ShapeType so the math-side
/// SDF helpers (`IRMath::SDF::evaluate`, `boundingHalf`, …) and the renderer
/// stay in lockstep without two parallel enums to keep synchronized.
using ShapeType = IRMath::SDF::ShapeType;

/// Bit-combinable shape flags stored in @c GPUShapeDescriptor::flags. The
/// canonical definition lives in @ref IRMath::SDF::ShapeFlags so the math
/// side and the renderer share one source of truth; the using-declarations
/// below re-export the @c SHAPE_FLAG_* enumerators into @c IRRender so the
/// existing @c IRRender::SHAPE_FLAG_VISIBLE spelling keeps working.
using ShapeFlags = IRMath::SDF::ShapeFlags;
using IRMath::SDF::SHAPE_FLAG_CHECKERBOARD;
using IRMath::SDF::SHAPE_FLAG_DEPTH_COLOR;
using IRMath::SDF::SHAPE_FLAG_HOLLOW;
using IRMath::SDF::SHAPE_FLAG_MIRROR_X;
using IRMath::SDF::SHAPE_FLAG_MIRROR_Y;
using IRMath::SDF::SHAPE_FLAG_NONE;
using IRMath::SDF::SHAPE_FLAG_VISIBLE;
using IRMath::SDF::SHAPE_FLAG_XRAY_OCCLUDED;

struct GPUShapeDescriptor {
    vec4 worldPosition;
    vec4 params;
    vec4 rotation;
    std::uint32_t shapeType;
    std::uint32_t color;
    std::uint32_t entityId;
    std::uint32_t jointIndex;
    std::uint32_t flags;
    std::uint32_t lodLevel;
    std::uint32_t _padding[2] = {};
};

struct GPUJointTransform {
    vec4 rotation;
    vec4 translation;
    std::uint32_t parentJointIndex;
    std::uint32_t _padding[3] = {};
};

struct GPUAnimationParams {
    float time;
    float speed;
    float phase;
    float _padding0;
    vec4 blend;
};

struct GPUShapesFrameData {
    vec2 cameraTrixelOffset;
    ivec2 trixelCanvasOffsetZ1;
    ivec2 canvasSize;
    int shapeCount;
    int passIndex;
    ivec2 voxelRenderOptions;
    ivec2 cullIsoMin;
    ivec2 cullIsoMax;
    // Z-yaw camera rotation, in radians. Mirrors FrameDataVoxelToCanvas:
    // visualYaw is the canonical continuous angle, rasterYaw is the cardinal
    // multiple of pi/2 nearest visualYaw, residualYaw = visualYaw - rasterYaw.
    // The shapes shader rasterizes at rasterYaw so the SDF surface lands on
    // the same integer voxel lattice as the voxel pool's cardinal-snap raster
    // (T-055), then applies faceDeform[face] to its sub-pixel offset to
    // recover continuous yaw geometrically (T-293, replaces the screen-space
    // bilinear residual composite from T-058 / T-322).
    float visualYaw = 0.0f;
    float rasterYaw = 0.0f;
    float residualYaw = 0.0f;
    // X dimension of the 2D dispatch grid used by SHAPES_TO_TRIXEL. The shader
    // computes tileIdx = gl_WorkGroupID.x + gl_WorkGroupID.y * tileGridX so
    // the dispatch stays within GL_MAX_COMPUTE_WORK_GROUP_COUNT[0].
    int tileGridX = 1;
    // Smooth camera Z-yaw (#1345): 1 enables the continuous-yaw SDF path
    // (full visualYaw query + continuous center reposition + shared world-space
    // x+y+z depth so the SDF composites with the per-axis voxel canvases, T3
    // #1310); 0 keeps the cardinal-snap rasterYaw + faceDeform path. Set per
    // canvas — only the rotating MAIN world canvas turns it on, so detached
    // per-entity canvases keep their faceDeform path. Occupies the first word of
    // the former 8-byte std140 alignment pad before faceDeform (faceDeform stays
    // at offset 80); the second word remains explicit pad.
    int smoothYawEnabled = 0;
    int _faceDeformPad_ = 0;
    // Per-face residual-yaw deformation packed column-major: .xy = col0,
    // .zw = col1 of IRMath::faceDeformationMatrix(face, residualYaw).
    // Identity (col0=(1,0), col1=(0,1)) when residualYaw == 0. Indexed by
    // IRMath::kXFace / kYFace / kZFace (0/1/2). std140 vec4 array stride
    // is 16 B so this is 48 B; mirrored as `vec4 faceDeform[3]` in the
    // GLSL/Metal UBO declarations.
    vec4 faceDeform[3] = {
        vec4(1.0f, 0.0f, 0.0f, 1.0f), vec4(1.0f, 0.0f, 0.0f, 1.0f), vec4(1.0f, 0.0f, 0.0f, 1.0f)
    };
};
static_assert(
    offsetof(GPUShapesFrameData, faceDeform) == 80,
    "GPUShapesFrameData::faceDeform must align at the 16-byte boundary "
    "GLSL std140 enforces for vec4 arr[3]"
);
static_assert(
    sizeof(GPUShapesFrameData) == 128, "GPUShapesFrameData size must mirror its std140 GLSL block"
);
static_assert(
    offsetof(FrameDataVoxelToCanvas, faceDeform_) == 80,
    "FrameDataVoxelToCanvas::faceDeform_ must align at the 16-byte boundary "
    "GLSL std140 enforces for vec4 arr[3]"
);
static_assert(
    offsetof(FrameDataVoxelToCanvas, visibleFaceIds_) == 128,
    "FrameDataVoxelToCanvas::visibleFaceIds_ must land at offset 128 "
    "(faceDeform_ at 80 + 3 * 16 B = 128). std140 ivec4 alignment is 16 B"
);
static_assert(
    offsetof(FrameDataVoxelToCanvas, voxelDepthAxis_) == 144,
    "FrameDataVoxelToCanvas::voxelDepthAxis_ must land at offset 144 "
    "(visibleFaceIds_ at 128 + 16 B). Appended after the prior last field so "
    "every existing offset — and the world/GRID fast path — stays unchanged"
);
static_assert(
    offsetof(FrameDataVoxelToCanvas, detachedWorldReceive_) == 160,
    "FrameDataVoxelToCanvas::detachedWorldReceive_ must land at offset 160 "
    "(voxelDepthAxis_ at 144 + 16 B). Appended after the prior last field so "
    "every existing offset — and the default screen-locked path — stays unchanged"
);
static_assert(
    offsetof(FrameDataVoxelToCanvas, visibleIsoBounds_) == 176,
    "FrameDataVoxelToCanvas::visibleIsoBounds_ must land at offset 176 "
    "(detachedWorldReceive_ at 160 + 16 B). Appended after the prior last field "
    "so every existing offset — and the shadows-off byte-identical path — stays "
    "unchanged"
);
static_assert(
    offsetof(FrameDataVoxelToCanvas, resolveMode_) == 192,
    "FrameDataVoxelToCanvas::resolveMode_ must land at offset 192 "
    "(visibleIsoBounds_ at 176 + 16 B). Appended after the prior last field so "
    "every existing offset — and the resolveMode==0 store path — stays unchanged"
);
static_assert(
    offsetof(FrameDataVoxelToCanvas, occlusionCullMipCount_) == 196,
    "FrameDataVoxelToCanvas::occlusionCullMipCount_ (#1812) must keep the "
    "first resolveMode tail pad slot (offset 196) it shipped on — "
    "c_voxel_visibility_compact reads it at that std140 offset"
);
static_assert(
    offsetof(FrameDataVoxelToCanvas, feederSubCap_) == 200 &&
        offsetof(FrameDataVoxelToCanvas, feederPassTailBase_) == 204,
    "FrameDataVoxelToCanvas feeder lanes (#2258 Step B) must occupy the two "
    "int slots after occlusionCullMipCount_ (offsets 200/204, shifted one "
    "slot down by the #1812 gate) — the compact + stage-1 GLSL/Metal blocks "
    "read them at those std140 offsets; no pad follows now the visible/feeder "
    "split is compile-time (IR_FEEDER_PASS)"
);
static_assert(
    offsetof(FrameDataVoxelToCanvas, overflowScratchLayout_) == 208,
    "FrameDataVoxelToCanvas::overflowScratchLayout_ (#2333) must land at offset "
    "208 (the #2258 feeder-partition block ends at 208). Appended after the prior "
    "last field so every existing offset — and the resolveMode==0 store path — "
    "stays unchanged"
);
static_assert(
    sizeof(FrameDataVoxelToCanvas) == 224,
    "FrameDataVoxelToCanvas size must mirror its std140 GLSL block "
    "(overflowScratchLayout_ ivec4 append: 208 + 16 = 224)"
);

struct FrameDataSun {
    // xyz = unit vector pointing from surfaces toward the sun; w unused.
    // Default mirrors RenderManager::m_sunDirection (overhead with small
    // -X / -Y tilt — those match the outward-normal signs of the visible
    // X_FACE / Y_FACE so dot-product shading produces Z > X > Y).
    // Live frame data is uploaded by BAKE_SUN_SHADOW_MAP each tick — this
    // default only matters before the first tick.
    vec4 sunDirection_ = vec4(-0.3f, -0.2f, -0.93f, 0.0f);
    float sunIntensity_ = 1.0f;
    float sunAmbient_ = 0.4f;
    int shadowsEnabled_ = 1;
    // Mirrors `RenderManager::m_aoEnabled`. When 0 the AO compute shader
    // short-circuits with a constant 1.0 (no darkening) so the lighting
    // pass treats AO as a no-op. Wired in here rather than in its own UBO
    // because every consumer (AO compute, lighting) already binds
    // FrameDataSun.
    int aoEnabled_ = 1;
    // Orthonormal basis perpendicular to sunDirection_, computed CPU-side
    // each frame in system_bake_sun_shadow_map. .w is std140 padding.
    vec4 sunBasisU_ = vec4(0.0f);
    vec4 sunBasisV_ = vec4(0.0f);
    // sunPx = floor((dot(p, uHat/vHat) - origin) / texelSize).
    // Legacy single-map fields — kept for backward compat; equal to
    // cascade 0 when CSM is active (cascadeCount_ == 2).
    vec2 sunBufferOriginUV_ = vec2(0.0f);
    vec2 sunBufferTexelSize_ = vec2(1.0f);
    // CSM: per-cascade AABB parameters. [0] = near, [1] = far.
    vec2 cascadeOriginUV_0_ = vec2(0.0f);
    vec2 cascadeTexelSize_0_ = vec2(1.0f);
    vec2 cascadeOriginUV_1_ = vec2(0.0f);
    vec2 cascadeTexelSize_1_ = vec2(1.0f);
    float cascadeSplitDepth_ = 0.0f;
    int cascadeCount_ = 1;
    // #2270 coverage-splat radius (sun texels): c_bake_sun_shadow_map atomicMin's
    // each caster's depth into a (2·r+1)² box, filling the sun texels a grazing /
    // point-scattered caster footprint leaves empty (the moth-eaten cast-shadow
    // holes). atomicMin makes the box a no-op where nearer geometry already
    // covers a texel, so a saturated-bake host is byte-identical and the fill
    // concentrates on genuinely-empty hole texels; doubles as the kill switch —
    // 0 ⇒ the exact single-write path. Engaged for the cardinal main-canvas bake
    // and the world-placed cast resolve; the C++ driver zeros it for the per-axis
    // resolve dispatch (structural per-axis / smooth-yaw byte-identity — see
    // system_bake_sun_shadow_map.hpp patchSunSplatRadius). See
    // docs/design/sun-shadow-bake-coverage.md. Occupies the trailing std140 pad
    // floats so the 128-byte layout is unchanged.
    float sunSplatMaxTexels_ = 6.0f;
    // Maximum shadow-throw window in sun-Z voxels: the receiver
    // (ir_sun_shadow_sample) rejects an occluder whose sun-Z gap exceeds this,
    // capping how far a caster throws its shadow. Set from the bake / feeder
    // AABB sweep distance (kSunShadowMaxDistance) by BAKE_SUN_SHADOW_MAP each
    // frame (#2320) so a caster the sweep bakes is receivable at its full throw
    // and the two cannot drift; a shorter window truncates a floating caster's
    // top-face shadow (its farthest-from-floor caster). Occupies the trailing
    // std140 pad float (128-byte layout unchanged); default only matters before
    // the first bake tick.
    float sunMaxShadowThrow_ = 64.0f;
};
static_assert(sizeof(FrameDataSun) == 128, "FrameDataSun must match std140 layout");
static_assert(offsetof(FrameDataSun, sunBasisU_) == 32, "sunBasisU_ must align after aoEnabled_");
static_assert(
    offsetof(FrameDataSun, sunBufferOriginUV_) == 64,
    "sunBufferOriginUV_ must align after sunBasisV_"
);
static_assert(
    offsetof(FrameDataSun, cascadeOriginUV_0_) == 80,
    "cascadeOriginUV_0_ must start after legacy fields"
);
static_assert(
    offsetof(FrameDataSun, cascadeOriginUV_1_) == 96, "cascadeOriginUV_1_ must align at offset 96"
);
static_assert(
    offsetof(FrameDataSun, cascadeSplitDepth_) == 112, "cascadeSplitDepth_ must align at offset 112"
);
static_assert(
    offsetof(FrameDataSun, cascadeCount_) == 116, "cascadeCount_ must align at offset 116"
);

/// @{
/// @name GPU buffer binding points
/// **CRITICAL:** These indices are hard-coded in both C++ and GLSL/MSL shaders.
/// A mismatch between C++ and the shader is **silent** — no error, just wrong
/// uniforms / garbage data. When adding or renaming a buffer, update the
/// corresponding @c binding or @c [[buffer(N)]] annotation in the shader as well.
constexpr std::uint32_t kBufferIndex_FrameDataUniform = 0;
constexpr std::uint32_t kBufferIndex_GlobalConstantsGLSL = 1;
constexpr std::uint32_t kBufferIndex_FramebufferFrameDataUniform = 2;
constexpr std::uint32_t kFramebufferFrameDataUniformBufferSize = sizeof(FrameDataFramebuffer);
constexpr std::uint32_t kBufferIndex_FrameDataUniformIsoTriangles = 3;
// Slot 4 was previously unused; the GPU light-volume seed pass reads
// `LightSourceBuffer` here. Metal caps buffer bindings at 0–30, which
// rules out the 31+ range that would otherwise be a more obvious home
// for this SSBO; future cleanup of the index map can renumber freely.
constexpr std::uint32_t kBufferIndex_LightSourceBuffer = 4;
constexpr std::uint32_t kBufferIndex_SingleVoxelPositions = 5;
constexpr std::uint32_t kBufferIndex_SingleVoxelColors = 6;
constexpr std::uint32_t kBufferIndex_FrameDataVoxelToCanvas = 7;
// Per-pool active-slot bitmask consumed by the visibility-compact compute
// pass (`c_voxel_visibility_compact.{glsl,metal}`). One uint32 per
// `kVoxelActiveMaskBits = 32` voxel slots; bit i mirrors
// `C_VoxelPool::m_voxelColors[i].color_.alpha_ != 0`. T-287 / #950.
// Repurposed from the legacy `VoxelSetUnlockedPositions` slot, which was
// declared but unused.
constexpr std::uint32_t kBufferIndex_VoxelActiveMask = 8;
constexpr std::uint32_t kBufferIndex_VoxelSetUnlockedColors = 9;
constexpr std::uint32_t kBufferIndex_FrameDataTrixelToTrixel = 10;
constexpr std::uint32_t kBufferIndex_FontData = 11;
constexpr std::uint32_t kBufferIndex_GlyphDrawCommands = 12;
constexpr std::uint32_t kBufferIndex_VoxelEntityIds = 13;
constexpr std::uint32_t kBufferIndex_HoveredEntityId = 14;
constexpr std::uint32_t kBufferIndex_DebugOverlayData = 15;
// Per-frame params (canvas quat + voxel count) for the detached re-voxelize GPU
// scatter compute (`c_revoxelize_detached`, #1556). Slot 16 was the only free
// index in the Metal 0–30 range; the compute reuses slot 17 (LocalVoxelPositions)
// for its per-pool resident locals SSBO, bound per-canvas before dispatch.
// On Metal, slot 16 ALSO carries the R32I image-atomic scratch
// (kMetalImageAtomicScratchSlot) for the raster kernels that declare it. The
// two never meet in one kernel: the scratch is bound per-kernel via
// metal_pipeline.cpp's functionUsesImageAtomicScratch (#1619 — the sticky
// unconditional scratch bind used to clobber this UBO for the fill dispatch).
constexpr std::uint32_t kBufferIndex_RevoxelizeDetachedParams = 16;
constexpr std::uint32_t kBufferIndex_LocalVoxelPositions = 17;
// Per-pool source occupancy+color grid for the detached re-voxelize INVERSE
// resample (#1619). Dense 3D grid keyed by integer source-local cell; two uints
// per cell ({colorPacked, materialFlagBone}, occupied iff the alpha byte of
// colorPacked != 0). The inverse-resample fill (`c_revoxelize_detached` mode 1)
// binds it here to answer `srcColor(roundHalfUp(R⁻¹·destCell))`. Aliases slot 9
// (the legacy, unused VoxelSetUnlockedColors slot) to stay inside Metal's 0–30
// binding cap; the fill is a standalone dispatch, so nothing else reads slot 9
// while it runs.
constexpr std::uint32_t kBufferIndex_RevoxelizeSourceGrid = kBufferIndex_VoxelSetUnlockedColors;
constexpr std::uint32_t kBufferIndex_EntityTransforms = 18;
constexpr std::uint32_t kBufferIndex_UpdateParams = 19;
constexpr std::uint32_t kBufferIndex_ShapeDescriptors = 20;
// SDF-shapes-path scaffolding for future joint deformation (c_shapes_to_trixel.glsl,
// slot 21). Not used by the voxel skinning path — voxels use slot 18
// (kBufferIndex_EntityTransforms) for skin matrices and slot 17
// (kBufferIndex_LocalVoxelPositions) for per-voxel bone-slot indices.
constexpr std::uint32_t kBufferIndex_JointTransforms = 21;
constexpr std::uint32_t kBufferIndex_AnimationParams = 22;
// Slot 23 was previously unused; reused for the GPU light-volume
// dilation chain's UBO. Same Metal-cap rationale as
// `kBufferIndex_LightSourceBuffer` above.
constexpr std::uint32_t kBufferIndex_LightVolumeParams = 23;
constexpr std::uint32_t kBufferIndex_ChunkVisibility = 24;
constexpr std::uint32_t kBufferIndex_CompactedVoxelIndices = 25;
constexpr std::uint32_t kBufferIndex_IndirectDispatchParams = 26;
constexpr std::uint32_t kBufferIndex_FrameDataLightingToTrixel = 27;
// Slot 28: feeds only the light-volume propagate shader (voxel-existence
// + SDF-blocker bits). Neither AO nor sun-shadow reads this bitfield SSBO
// (see `kBufferIndex_SunShadowDepthMap` below for the slot-28 alias).
constexpr std::uint32_t kBufferIndex_LightOcclusionGrid = 28;
constexpr std::uint32_t kBufferIndex_FrameDataSun = 29;
constexpr std::uint32_t kBufferIndex_ShapeTileDescriptors = 30;
// Live analytic fog vision circles (FOG_TO_TRIXEL). A tiny per-canvas UBO
// (a handful of vec4 circles + a count) the fog pass reads to mask the scene
// by a smooth, render-resolution vision disc evaluated per pixel from the
// continuous world column — distinct from the voxel-grid fog texture (binding
// 2), which carries only the coarse explored/voxelized memory. Uploaded every
// frame (small, unconditional) so no dirty flag is needed.
//
// ALIASES slot 27 (FrameDataLightingToTrixel): the Metal 0-30 buffer table is
// full, so a new pass must reuse a slot. FOG_TO_TRIXEL is documented to run
// immediately after LIGHTING_TO_TRIXEL (it masks the *lit* canvas), so by the
// time fog dispatches, lighting has finished consuming slot 27 this frame; no
// stage between fog and next-frame lighting reads it, and lighting rebinds 27
// before its own dispatch (the engine's rebind-before-use discipline). Both
// shaders hard-code `27` for this UBO — keep them in lockstep with this alias.
constexpr std::uint32_t kBufferIndex_FogObservers = kBufferIndex_FrameDataLightingToTrixel;
// Aliases the light-occlusion-grid slot. The light-volume propagate
// shader reads LightOcclusionGrid; the sun bake writes /
// the sun shadow lookup reads SunShadowDepthMap. Both consumers run on
// different stages and rebind slot 28 to whichever resource they need
// before their own dispatch, so the alias is safe. Phased-out producer:
// this aliasing goes away in T-09Y once light-volume LOS moves off the
// world-space bitfield.
constexpr std::uint32_t kBufferIndex_SunShadowDepthMap = kBufferIndex_LightOcclusionGrid;
// Aliases slot 28 again. RESOLVE_PER_AXIS_SCREEN_DEPTH (#1435) scatters the
// three per-axis voxel canvases into this scratch SSBO via imageAtomicMin
// (a screen-space front-most iso-depth, main-canvas sized), then blits it
// into the per-axis resolve TEXTURE. The whole resolve runs as its own
// pipeline stage strictly before BAKE_SUN_SHADOW_MAP rebinds slot 28 to the
// SunShadowDepthMap, so the alias is safe — same non-overlapping-stage
// rationale as the LightOcclusionGrid/SunShadowDepthMap alias above. The
// scratch lives on a buffer because Metal has no portable image-atomic
// syntax (see c_voxel_to_trixel_stage_1.metal's distance scratch).
// VOXEL_TO_TRIXEL_STAGE_1's per-axis winner election (#2255) is a THIRD
// transient consumer: it binds its winner-election scratch (winnerIds_) here
// for the deterministic color-winner atomicMin, then reads the settled winner
// back. Both the write and the read complete inside that one system's per-axis
// loop, which registers strictly before every slot-28 lighting/occlusion/
// resolve/bake consumer rebinds — same non-overlapping-stage safety, but a
// narrower borrow than the resolve above: it never hands the resource across a
// stage boundary.
constexpr std::uint32_t kBufferIndex_PerAxisResolveScratch = kBufferIndex_LightOcclusionGrid;
// Per-axis empty-cell compaction (#1961). The TRIXEL_TO_FRAMEBUFFER composite
// compacts each per-axis canvas's occupied cells into an indirect instanced
// draw, so the scatter rasterizes only non-empty cells instead of sweeping the
// whole worst-case-sized per-axis grid (~12x the cardinal area, mostly empty).
// Aliases slots 25/26: the voxel compaction (VOXEL_TO_TRIXEL_STAGE_1/2) writes
// and consumes them several stages earlier, and SPRITE_TO_SCREEN (the other
// slot-25 consumer) draws after the main framebuffer — same non-overlapping-
// stage rationale as the aliases above. CompactedCells is bound as an SSBO for
// both the compaction write and the scatter vertex-shader read; CellIndirect is
// bound as an SSBO for the compaction write and as the draw-indirect buffer for
// the issue.
constexpr std::uint32_t kBufferIndex_PerAxisCellCompacted = kBufferIndex_CompactedVoxelIndices;
constexpr std::uint32_t kBufferIndex_PerAxisCellIndirect = kBufferIndex_IndirectDispatchParams;
// SPRITE_TO_SCREEN aliases two slots whose prior consumers finish before the
// sprite draw. Safety is enforced by a defensive rebind in
// `SPRITE_TO_SCREEN::bindPipeline()` — both slots are re-asserted to the
// sprite resources immediately before each draw call, displacing any earlier
// occupant. Slot 0 (FrameDataUniform) is also used by the T-163 stateless
// particle UBO; slot 25 (CompactedVoxelIndices) is written by
// VOXEL_TO_TRIXEL_STAGE_1 and consumed by STAGE_2. Same Metal 0–30 cap
// rationale as `kBufferIndex_SunShadowDepthMap`.
constexpr std::uint32_t kBufferIndex_SpritesFrameData = kBufferIndex_FrameDataUniform;
constexpr std::uint32_t kBufferIndex_SpritesInstances = kBufferIndex_CompactedVoxelIndices;
// GPU particle slots (T-139 Phase 1). Metal caps bindings at 0–30, so we
// alias the particle SSBO and UBO onto slots whose other consumers run on
// non-overlapping compute encoders. Both `LightSourceBuffer` (slot 4) and
// `LightVolumeParams` (slot 23) are bound only by the COMPUTE_LIGHT_VOLUME
// stage's seed/propagate dispatches; the particle update and particle render
// passes never run inside that stage's encoder, so the rebind is safe (same
// rationale as `kBufferIndex_SunShadowDepthMap` above). Note: a creation
// that registers BOTH a particle render system and COMPUTE_LIGHT_VOLUME on
// the same canvas must order them so neither dispatch is in flight when the
// other rebinds — the established pipeline order
// (COMPUTE_LIGHT_VOLUME → LIGHTING_TO_TRIXEL → particle render) satisfies
// this.
constexpr std::uint32_t kBufferIndex_GpuParticleData = kBufferIndex_LightSourceBuffer;
constexpr std::uint32_t kBufferIndex_FrameDataGpuParticles = kBufferIndex_LightVolumeParams;
// Stateless particle slots (T-163 Phase 1). Split into a small per-frame
// UBO (header: currentTime, emitterCount, projection inputs) and a separate
// SSBO holding the emitter descriptor array. Splitting sidesteps the
// observed Metal-side flakiness when nested-struct arrays live in a
// `constant` (UBO) buffer at this size class — the SSBO path uses
// straightforward `device` storage with no implicit layout assumptions.
//
// UBO slot: aliases the long-reserved-but-unused `FrameDataUniform` slot 0
// — same slot the sprite pipeline borrows for `SpritesFrameData`. The
// aliasing is safe because `SPRITE_TO_SCREEN::bindPipeline()` defensively
// rebinds slot 0 to `SpritesFrameData` immediately before each draw call,
// so any prior stateless-particle UBO occupying slot 0 is always displaced
// before the sprite vertex shader reads it (OpenGL has global binding
// state; Metal compute and render encoders maintain independent argument
// tables, so the alias is inherently safe there).
// SSBO slot: aliases `kBufferIndex_LightSourceBuffer` (slot 4), already
// shared with `kBufferIndex_GpuParticleData` — both T-139 SSBO particles
// and T-163 stateless emitters can register on the same canvas, and each
// dispatch rebinds slot 4 to its own SSBO immediately before its dispatch
// (the established trixel pipeline order COMPUTE_LIGHT_VOLUME → particle
// passes guarantees the light volume's seed dispatch finishes before
// either particle pass binds the slot).
constexpr std::uint32_t kBufferIndex_FrameDataStatelessParticles = kBufferIndex_FrameDataUniform;
constexpr std::uint32_t kBufferIndex_StatelessParticleEmitters = kBufferIndex_LightSourceBuffer;
/// @}

/// Maximum number of light sources uploaded per frame to the
/// `LightSourceBuffer` SSBO consumed by the GPU light-volume seed pass.
/// Lights past this cap are silently dropped on the CPU side; the cap is
/// generous for the engine's current "few dozen lights" workloads.
constexpr std::uint32_t kLightVolumeMaxSources = 256;

/// Upper bound on GPU dilation iterations the propagate pass dispatches
/// each frame. `system_compute_light_volume` picks an adaptive iteration
/// count per frame from the gathered lights' max `C_LightSource::radius_`
/// and clamps it to this constant — small-radius scenes pay only for the
/// iterations they need, while pathological per-cell costs on weaker GPU
/// drivers (WSLg Mesa-d3d12, integrated Intel GL) stay bounded. Phase 1c
/// will replace the global cap with per-light step counts so multi-light
/// scenes with very different radii stop sharing a single falloff curve.
constexpr int kLightVolumePropagateIterations = 32;

/// CPU mirror of the `LightSource` GPU struct uploaded to the
/// `LightSourceBuffer` SSBO. One entry per active `C_LightSource`
/// entity. Layout follows std430: every member is a `vec4` so the GPU
/// stride is 80 bytes per record. Decoded in `c_seed_light_volume` (seed
/// / propagate) and `c_lighting_to_trixel` (spot-cone consume, #2318).
struct GPULightSource {
    /// xyz = the volume texel origin the seed writes to: the light's
    /// world voxel origin (round-half-up of `C_WorldTransform.translation_`)
    /// for in-window lights, or the per-axis-clamped window-boundary cell for
    /// an out-of-window light (see `gatherLightSources`); w = `LightType`
    /// cast to float. This is the SEED cell, NOT the light's apex — the spot
    /// cone reads `trueOriginVoxel_` for the true apex.
    vec4 originAndType_ = vec4(0.0f);
    /// xyz = emissive RGB in [0, 1]; w = intensity scalar.
    vec4 colorAndIntensity_ = vec4(0.0f);
    /// xyz = unit direction (SPOT cone axis / DIRECTIONAL ray; unused for
    /// EMISSIVE / POINT); w = radius in voxel cells (clamped to
    /// `kLightVolumePropagateIterations`).
    vec4 directionAndRadius_ = vec4(0.0f);
    /// x = cone aperture in degrees (SPOT only); y = seed residual alpha
    /// in (0, 1] — 1.0 for lights inside the camera-anchored window,
    /// boundary-distance-discounted for lights seeded at the clamped
    /// window edge (see `gatherLightSources`); zw = std430 padding.
    vec4 coneAndSeedAlpha_ = vec4(0.0f);
    /// xyz = the light's TRUE world voxel origin (unclamped apex), used by
    /// `c_lighting_to_trixel`'s spot-cone factor so an out-of-window spot's
    /// cone stays oriented from its real apex rather than the clamped seed
    /// cell (#2318, winning-light ID channel). Equals `originAndType_.xyz`
    /// for in-window lights. w = std430 padding.
    vec4 trueOriginVoxel_ = vec4(0.0f);
};
static_assert(sizeof(GPULightSource) == 80, "GPULightSource must match std430 layout");

/// CPU mirror of the propagate pass UBO. Uploaded each frame by
/// `system_compute_light_volume`. Read by `c_seed_light_volume.glsl`,
/// `c_propagate_light_volume.glsl`, and `c_lighting_to_trixel.glsl`.
struct LightVolumeParams {
    /// Must match `kLightVolumeSize` in
    /// `component_canvas_light_volume.hpp` (128 today).
    int gridSize_ = 128;
    /// `kLightVolumeSize / 2` — half-extent for world ↔ texel offset.
    int halfExtent_ = 64;
    /// Number of valid entries in the `LightSourceBuffer` SSBO this frame.
    int lightCount_ = 0;
    /// Per-step alpha decrement applied during distance-tracked
    /// propagation. The propagate shader stores residual strength in
    /// the alpha channel; each Manhattan step subtracts this value, so
    /// a light reaches `1 / stepFalloff_` cells before going dark
    /// (linear falloff, matching the CPU BFS's `1 - d/radius` curve).
    /// Written per frame by `system_compute_light_volume`: paired with
    /// the adaptive iteration count, falloff is `1 / iterations` so
    /// alpha lands cleanly at 0 on the final propagate step. Multi-light
    /// scenes with very different radii share the gather's max-radius
    /// falloff curve until Phase 1c per-light step counts land.
    /// The struct default mirrors the global cap so a fresh `LightVolumeParams{}`
    /// behaves like today's 32-cell radius before the first per-frame write.
    float stepFalloff_ = 1.0f / 32.0f;
    /// Phase 1c (#360): camera-anchored origin. The 128³ light volume
    /// is centered on this world voxel each frame so a panned camera
    /// keeps lights in-range. Stored as `ivec4` for std140 alignment;
    /// `.xyz` is the volume origin, `.w` is the has-SPOT flag (#2318).
    ivec4 worldOriginVoxel_ = ivec4(0);
};
static_assert(sizeof(LightVolumeParams) == 32, "LightVolumeParams must match std140 layout");

/// Phase 1c (#360): camera-anchored light-occlusion SSBO header.
/// Written to the first 16 bytes of `LightOcclusionGridBuffer` each
/// frame by `system_build_light_occlusion_grid`; the voxel + SDF-blocker
/// bitfields occupy the remainder (see `kLightOcclusionHeaderByteSize`
/// consumers in `system_build_light_occlusion_grid.hpp` and the SSBO
/// declarations in `c_propagate_light_volume.glsl` /
/// `metal/c_propagate_light_volume.metal`). The header avoids a second
/// buffer slot — Metal compute encoders share one global
/// `setBuffer(slot)` table per encoder, so a UBO and SSBO at the same
/// slot fight; embedding the header in the SSBO sidesteps the conflict
/// entirely. `.xyz` is the world voxel that maps to local cell
/// `(0,0,0)`; `.w` is reserved.
struct LightOcclusionGridHeader {
    ivec4 worldOriginVoxel_ = ivec4(0);
};
static_assert(
    sizeof(LightOcclusionGridHeader) == 16, "LightOcclusionGridHeader must match std430 layout"
);

// One entry per dispatched tile in the batched shapes→trixel pass.
// shapeIndex picks the ShapeDescriptor; tileIsoOrigin is the iso-space
// origin of this tile's 8×8 pixel footprint (already pre-aligned on CPU).
struct ShapeTileDescriptor {
    int shapeIndex = 0;
    int _pad0 = 0;
    ivec2 tileIsoOrigin = ivec2(0);
};

/// Single GPU particle record uploaded to the `GpuParticleData` SSBO. Phase 1
/// of T-139 — position + velocity drift + lifetime decay; spawn / collection
/// query / attraction-point fields land in subsequent phases.
///
/// Layout matches std430:
///   offset 0..11   position_  (vec3, 12 B)
///   offset 12..15  lifetime_  (float, 4 B — fills vec3 trailing pad)
///   offset 16..27  velocity_  (vec3, 12 B)
///   offset 28..31  color_     (uint32, 4 B — fills vec3 trailing pad)
/// Total 32 B per record. See `c_update_gpu_particles.glsl` and
/// `c_render_gpu_particles_to_trixel.glsl` for the GPU mirror.
///
/// Lifetime semantics: `lifetime_ <= 0.0f` means the slot is dead; both the
/// update and render compute kernels skip such slots. Setting `lifetime_ = 0`
/// is the canonical way to despawn from the CPU side.
struct GpuParticle {
    vec3 position_ = vec3(0.0f);
    float lifetime_ = 0.0f;
    vec3 velocity_ = vec3(0.0f);
    std::uint32_t color_ = 0u;
};
static_assert(sizeof(GpuParticle) == 32, "GpuParticle must match std430 layout");

/// Per-frame UBO for both the update and render-to-trixel particle compute
/// passes. The render pass ignores the `*Update*` fields and vice versa, but
/// they share one UBO so a single CPU upload feeds both dispatches.
struct FrameDataGpuParticles {
    // Update-pass fields:
    float deltaTime_ = 0.0f;
    std::uint32_t particleCount_ = 0u;
    std::uint32_t _updatePad0_ = 0u;
    std::uint32_t _updatePad1_ = 0u;
    // Render-pass fields (mirror the trixel-canvas projection inputs):
    vec2 cameraTrixelOffset_ = vec2(0.0f);
    ivec2 trixelCanvasOffsetZ1_ = ivec2(0);
    ivec2 canvasSizePixels_ = ivec2(0);
    int _renderPad0_ = 0;
    int _renderPad1_ = 0;
};
static_assert(
    sizeof(FrameDataGpuParticles) == 48, "FrameDataGpuParticles must match std140 layout"
);

/// GPU particle pool capacity per pool entity. Phase 1 caps the pool at this
/// fixed size; per-biome configurable capacity lands in Phase 2.
constexpr std::uint32_t kGpuParticlePoolCapacity = 4096u;

/// Cap on the number of stateless emitters per canvas. The 32 B header lives
/// in a UBO (slot 0); the emitter descriptors live in an SSBO (slot 4) —
/// only the header is subject to the 16 KB UBO guarantee. At 64 emitters
/// × 80 B = 5 120 B, the SSBO is a comfortable fit for Phase 1 workloads.
/// Tunable via a Phase 2 follow-up when real biome workloads land.
constexpr std::uint32_t kMaxStatelessEmitters = 64u;

/// Cap on particles per stateless emitter. The render dispatch fires
/// `emitterCount * kMaxParticlesPerEmitter` threads; threads with
/// `subIndex >= particlesPerEmitter` early-out, so a per-emitter
/// runtime cap of less than this constant pays its own way.
/// SYNC: kMaxParticlesPerEmitter must match the identically-named define in
/// c_render_stateless_particles_to_trixel.glsl and the Metal constant in
/// c_render_stateless_particles_to_trixel.metal — all three decompose gid
/// the same way; a value change in one that misses the others silently
/// breaks thread ID decomposition.
constexpr std::uint32_t kMaxParticlesPerEmitter = 256u;

/// T-163 Phase 1 — single stateless particle emitter descriptor. Particles
/// have no per-frame stored state; each shader thread reconstructs its
/// particle's position and color from `(emitter, subIndex, currentTime)` via
/// a closed-form gravity-with-jitter trajectory. The descriptor is purely an
/// input — the GPU never mutates it.
///
/// Layout is std430 (vec3 fields naturally followed by a trailing float use
/// the 4-byte pad slot, so each row is 16 B — coincidentally std140-
/// compatible). Five rows total = 80 B per emitter.
///   row 0: origin_.xyz                 | baseLifetimeSeconds_
///   row 1: baseVelocity_.xyz           | spawnRate_
///   row 2: gravity_.xyz                | baseColorPacked_
///   row 3: positionJitter_.xyz         | emitterFlags_
///   row 4: velocityJitter_.xyz         | particlesPerEmitter_
struct GpuParticleEmitter {
    vec3 origin_ = vec3(0.0f);
    float baseLifetimeSeconds_ = 1.0f;
    vec3 baseVelocity_ = vec3(0.0f);
    float spawnRate_ = 1.0f;
    vec3 gravity_ = vec3(0.0f);
    std::uint32_t baseColorPacked_ = 0u;
    vec3 positionJitter_ = vec3(0.0f);
    std::uint32_t emitterFlags_ = 0u;
    vec3 velocityJitter_ = vec3(0.0f);
    std::uint32_t particlesPerEmitter_ = 0u;
};
static_assert(
    sizeof(GpuParticleEmitter) == 80,
    "GpuParticleEmitter must match std430 layout (80 B per emitter)"
);

/// T-163 Phase 1 — per-frame UBO for the stateless particle render pass.
/// Header only: per-frame inputs (`currentTime_`, canvas projection
/// parameters) plus emitter count. The descriptor array lives in a
/// separate SSBO so the layout is straightforward `device` storage on
/// Metal rather than `constant` (UBO) storage, which sidestepped layout
/// flakiness with nested-struct arrays during Phase 1 bring-up.
///
/// `voxelRenderOptions_` mirrors `FrameDataVoxelToCanvas::voxelRenderOptions_`
/// (renderMode, effectiveSubdivisions). The particle pass reads it so each
/// particle paints a subdivision-scaled voxel diamond — same micro-position
/// walk the voxel pool uses — instead of a single 2×3 diamond regardless of
/// zoom. Without this the particle "voxels" stay at base resolution while
/// the voxel/SDF paths refine to sub² micro-cells per voxel under FULL mode,
/// and the two read as different sizes in the same frame.
struct FrameDataStatelessParticles {
    float currentTime_ = 0.0f;
    std::uint32_t emitterCount_ = 0u;
    vec2 cameraTrixelOffset_ = vec2(0.0f);
    ivec2 trixelCanvasOffsetZ1_ = ivec2(0);
    ivec2 canvasSizePixels_ = ivec2(0);
    ivec2 voxelRenderOptions_ = ivec2(0);
    ivec2 _padding_ = ivec2(0);
};
static_assert(
    sizeof(FrameDataStatelessParticles) == 48,
    "FrameDataStatelessParticles must match std140 layout (48 B header)"
);

struct VoxelIndirectDispatchParams {
    std::uint32_t numGroupsX = 0;
    std::uint32_t numGroupsY = 0;
    std::uint32_t numGroupsZ = 0;
    std::uint32_t visibleCount = 0;
    std::uint32_t completedGroups = 0;
    std::uint32_t _padding[3] = {};
};

// GPU indirect draw-args for the per-axis empty-cell compaction composite
// (#1961). The leading five uints are byte-identical to GL's
// DrawElementsIndirectCommand and Metal's MTLDrawIndexedPrimitivesIndirectArguments,
// so one compaction kernel fills a buffer both backends issue an indirect
// instanced draw from. The compaction resets instanceCount to 0 and
// atomic-appends one occupied cell per increment (the append index is the
// instance id); indexCount is the quad index count, the rest stay 0. Padded to
// 32 B; the three per-axis structs are spaced kPerAxisCellIndirectStrideBytes
// apart so each sits at an SSBO-bindRange-aligned offset for the compaction
// write (mirrors VoxelIndirectDispatchParams's kPerAxisSsboAlignBytes spacing).
struct PerAxisCellDrawCommand {
    std::uint32_t indexCount = 0;
    std::uint32_t instanceCount = 0;
    std::uint32_t firstIndex = 0;
    std::uint32_t baseVertex = 0;
    std::uint32_t baseInstance = 0;
    std::uint32_t _padding[3] = {};
};
constexpr std::ptrdiff_t kPerAxisCellIndirectStrideBytes = 256;

// Per-axis empty-cell compaction (#1961) additionally feeds the per-axis GPU
// compute stages (AO / sun-shadow / lighting / resolve-scatter) via indirect
// COMPUTE dispatch over the same compacted cell list (#2256), so those stages
// process only occupied cells instead of the full worst-case (2W)(W+H) grid.
// A VoxelIndirectDispatchParams block (numGroupsX/Y/Z, visibleCount) sits in each
// axis's 256 B slot-26 region at a fixed offset ABOVE the 32 B
// PerAxisCellDrawCommand so the draw-indirect args and the compute-indirect args
// coexist in one region. The compaction (c_per_axis_cell_compact) only
// atomic-appends into instanceCount; a separate cheap c_per_axis_cell_finalize
// pass then derives numGroupsX/Y/Z + visibleCount from that final count (split
// out to keep the compaction's hot full-grid scan barrier-free).
// `dispatchComputeIndirect` reads numGroupsX/Y/Z from that offset; the compute
// kernels read `visibleCount` from the same SSBO region for the in-shader bound
// guard.
constexpr std::ptrdiff_t kPerAxisCellDispatchArgsOffsetBytes = 32;
// Threads per per-axis compute workgroup. c_per_axis_cell_finalize sets numGroups
// to a capped 2-D grid of divCeil(occupiedCount, kPerAxisCellComputeTile)
// workgroups; each consumer kernel recovers its flat list index as
// (groupId.x + groupId.y*numGroupsX)*tile + localInvocationIndex. This value has
// no compile-time tie to the shaders — it must stay 16*16 to match the
// local_size_x/y = 16 declared independently in each of the four consumer
// kernels' GLSL + Metal twins (c_compute_voxel_ao, c_compute_sun_shadow,
// c_lighting_to_trixel, c_resolve_per_axis_screen_depth). Changing any one
// kernel's group size without updating this constant AND the other seven files
// silently misaligns the cell recovery (skipped or duplicated cells, no
// compiler/runtime diagnostic).
constexpr std::uint32_t kPerAxisCellComputeTile = 256;

// TODO: Future culling optimization constants
// Chunk-level frustum culling: voxel pool is partitioned into chunks of
// this size. A CPU-side visibility pass writes a per-chunk mask that the
// voxel-to-trixel shaders check for early-out.
constexpr int kVoxelChunkSize = 256;

} // namespace IRRender

#endif /* IR_RENDER_TYPES_H */
