#ifndef COMPONENT_CANVAS_FOG_OF_WAR_H
#define COMPONENT_CANVAS_FOG_OF_WAR_H

// World-space 2D fog-of-war visibility texture sampled by FOG_TO_TRIXEL
// to mask the rendered scene by per-column visibility state. One cell
// per voxel column on the iso ground plane (X-Y axes, since +Z is the
// downward height axis in this engine's iso convention). Three states:
// `kFogStateUnexplored` = never seen, `kFogStateExplored` = seen but
// not currently visible, `kFogStateVisible` = currently in vision.
//
// Two reveal mechanisms compose here, max-combined in the shader:
//   * The voxel GRID (this texture): coarse, voxel-quantized state set by
//     `setCell` / `revealRadius`. Good for an arbitrary accumulated EXPLORED
//     "memory" union and a hard voxelized vision circle.
//   * Live analytic VISION CIRCLES (`FrameDataFogObservers`, below): smooth
//     world-space discs evaluated per pixel in the shader, so a moving
//     observer's "currently visible" disc is crisp at render resolution and
//     reveals partial voxels — what the grid cannot express. See that struct.
//
// Format is RGBA8 rather than R8 so the Metal backend's rgba8 image
// binding path can share a single binding-layout with the AO and sun-
// shadow textures (see C_CanvasSunShadow for the same trade-off). Only
// the .r channel carries fog state; the other channels are written 0
// and unused.
//
// The CPU state is `IRPrefab::Fog::WorldField` (`render/fog_world_field.hpp`):
// unbounded, world-space, optionally persisted, held through a shared handle
// so ECS copies alias it as they alias the texture. The texture is a
// camera-anchored window over it. Every mutation lands in the field's pending
// field-chunk set; VOXEL_TO_TRIXEL_STAGE_1 drains that set once per frame,
// re-expands the pending field chunks inside the window (and the strip a
// window move exposes) and uploads one rectangle per run, before using fog to
// cull unexplored columns. FOG_TO_TRIXEL is a read-only consumer. Population
// is driver-side: gameplay calls `IRPrefab::Fog::setCell` /
// `IRPrefab::Fog::revealRadius` (see `render/fog_of_war.hpp`) to drive the
// visibility set directly.
//
// The window is `windowEdge_` texels square, one texel per integer voxel
// column, sized at construction from the canvas footprint
// (`IRPrefab::Fog::detail::windowEdgeForCanvas`) so every column whose matter
// in the camera depth slab can reach the canvas is inside it. Its origin
// (`windowOrigin_`, the field column at local (0, 0)) is snapped to field
// chunks around the world point under the viewport centre and rides the
// observer UBO's `windowOriginX_` / `windowOriginY_` lanes, written by the
// gather in the same frame as the texture contents. Addressing is toroidal:
// column `c` is at texel `floorMod(c, windowEdge_)` whatever the origin, so a
// move re-expands only the exposed strip. A column outside the window reads
// UNEXPLORED in every shader tap (the analytic circles still compose over
// it); the 1×1 placeholder a non-fog canvas binds still reads visible.
// Contract: docs/design/fog-of-war-world-field.md (D6, D10).
//
// `kFogOfWarSize` / `kFogOfWarHalfExtent` are the legacy fixed window's
// dimensions, kept as deprecated values for out-of-tree callers; no engine
// code or shader reads them. The line-of-sight field has its own
// `kFogLosTextureSize`.
//
// Line of sight. A vision circle opted in with `setVisionCircleLineOfSight`
// reveals only what its eye can see over a 2.5D column model, evaluated
// exactly at every sample rather than once per cell:
//   * Occluders: the active grid canvas's pool voxels with alpha > 0 and no
//     `VoxelReserved::kFogWholeBodyExempt` (a governed body never occludes),
//     plus every `C_ShapeDescriptor + C_LightBlocker{blocksLOS_} +
//     C_WorldTransform` shape on that canvas. Each occupies the box the
//     raster draws it as, in the world coordinates its pixels recover: a
//     voxel at position `p` fills the unit cube on its lower-corner lattice
//     (`[p, p + 1]` at yaw 0, after the raster's subdivision snap and
//     cardinal rotation), a shape its drawn surface about its cardinal-snap
//     origin. A column is opaque downward from its highest occupied top plane
//     `T(c)` (the smallest Z; +Z is down), stored per half-cell so both
//     integer- and half-integer-positioned voxel sets sit exactly on the
//     lattice. Overhangs, caves and detached canvases are out of the model.
//   * Eye: `(cx, cy, observerZ - losEyeHeight)`, continuous, and above the
//     column it stands in; that column never occludes, so an ungoverned body
//     spanning several half-cells occludes unless the eye clears its top.
//   * Gate: walk the XY segment from the eye to the sample through the
//     half-cell lattice. A column blocks when the segment's height at the
//     point of the column's footprint where it is lowest is at or below the
//     column's top plane; a segment that reaches the sample's own column
//     from above keeps it visible, and a segment that reaches it from
//     below hides it, so a face seen from behind or a wall top seen from
//     below hides without any per-face rule. The clearance is the smallest
//     gap the segment keeps above any column it crosses; the hard verdict is
//     `clearance >= -kFogLosClearanceTolerance`, and a source's softness `s`
//     grades it as `smoothstep(0, s, clearance)`. The walk steps over blocks
//     of a pyramid of the field that cannot change that verdict
//     (`fog_line_of_sight.hpp`), which changes its cost, not its result.
//   * Sample: every route evaluates its pixel's recovered world position on
//     that lattice (`fogLosCanonicalSample`): the cardinal voxel raster
//     recovers a face's pixels up to a micro cell off the face plane, so
//     their face-axis coordinate snaps onto it; a per-axis cell and an
//     analytic pixel (the shape raster's carrier bit) resolve to their authored
//     surfaces after the analytic cardinal key's raster-lattice displacement
//     is removed. Every sample then steps a hair out along its face normal. The
//     CPU oracle (`IRPrefab::Fog::losVisibility`) evaluates an entity at its
//     ground anchor, lifted onto its column's top plane when it sits below it.
// `FOG_LOS_BUILD` rebuilds the column field each RENDER frame and uploads
// `losTexture_` (256 × 510 RGBA32F: the four half-cells of each integer cell
// in one texel's four channels, then the field's pyramid; `kFogLosColumnEmpty`
// = no occluder). The field covers 256 × 256 cells anchored with the fog
// window: its lower corner is `FogLosColumnField::fieldMinForWindow` of the
// window's origin and edge — the window's centre less the field's half
// extent, snapped down to the coarsest pyramid block so every block stays
// whole — derived identically on the CPU and in `ir_fog_los.{glsl,metal}`
// from the observer block's origin lanes and the fog texture's edge.
// `FOG_LOS_BUILD` runs before the frame's gather writes those lanes, so it
// anchors on the origin the gather is about to write
// (`IRPrefab::Fog::detail::cameraWindowOrigin`). Columns outside the field are
// empty, and occluders outside it are unknown.

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/fog_world_field.hpp>
#include <irreden/render/texture.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <vector>

using namespace IRMath;
using namespace IRRender;

namespace IRComponents {

/// Deprecated: the edge and half extent of the legacy fixed fog window. The
/// window is now sized per canvas (`C_CanvasFogOfWar::windowEdge_`) and
/// anchored on the camera; nothing in the engine reads these.
constexpr int kFogOfWarSize = 256;
constexpr int kFogOfWarHalfExtent = kFogOfWarSize / 2;

// The one reveal channel the engine assigns. Grid cells and every vision
// source reveal on it; a creation's own channel bits are its own to define.
constexpr std::uint32_t kFogChannelDefault = 1u;

// Live analytic "vision circle" reveal — the smooth, render-resolution path
// that the voxel grid above cannot express. Each circle is a world-space disc
// (center + radius) the fog shader evaluates PER PIXEL from the continuous
// world column (`pos3D.xy`), so the edge is crisp at game resolution, slides
// smoothly with sub-voxel observer motion, and reveals partial voxels at the
// boundary (a voxel straddling the disc has some pixels inside, some out). The
// grid (the texture below) carries only coarse explored/voxelized memory; the
// circles carry "currently visible". The two are max-combined in the shader.
//
// `kFogVisionEdgeDefault` is the edge softness in WORLD units. 0 = a crisp,
// zoom-stable ~1px antialiased rim: the shader floors the edge at the local
// world-per-pixel size (recovered from the iso inverse-projection Jacobian), so
// the circle stays sharp at every zoom and never aliases. A positive value adds
// a deliberately soft falloff of that many world units on top. Up to
// `kMaxFogVisionCircles` sources compose via max (player + a few allies/lights);
// every later source is field-tier: its XY disc is stamped into the world
// field's transient layer (see `C_CanvasFogOfWar::addVisionCircle`).
//
// `kMaxFogVisionCircles` is mirrored as a literal in `ir_fog_common.glsl` /
// `metal/ir_fog_common.metal` (the UBO array length); changing it requires
// editing both shaders and re-checking the std140 / Metal struct size below.
constexpr int kMaxFogVisionCircles = 8;
constexpr float kFogVisionEdgeDefault = 0.0f;
// Sentinel for `addVisionCircle`'s zCostDown param: any value < 0 mirrors the
// already-clamped zCostUp (see the sentinel-before-clamp note there).
constexpr float kFogVisionZCostMirrorUp = -1.0f;
// `setVisionCircleLineOfSight` eye height that disables line of sight for a
// source; any value >= 0 enables it.
constexpr float kFogVisionLosOff = -1.0f;
// `setVisionCircleLineOfSight` softness that keeps the gate hard; a positive
// value grades the verdict over that many world units of clearance.
constexpr float kFogLosHardGate = 0.0f;

// Half-cells per world unit of the column field: a voxel box edge lands on
// the lattice whether its position is integer or half-integer.
constexpr int kFogLosCellsPerUnit = 2;
// The field is 256 cells on a side, `kFogLosTextureSize` texels across.
constexpr int kFogLosTextureSize = 256;
constexpr int kFogLosFieldSize = kFogLosTextureSize * kFogLosCellsPerUnit;
constexpr int kFogLosFieldHalfExtent = kFogLosFieldSize / 2;
// The upload image packs each integer cell's four half-cells into one RGBA
// texel, channel `(hx & 1) + 2 * (hy & 1)` (`kFogLosTextureSize` texels
// across), and stacks the field's pyramid under that level 0: level `k` holds
// one value per block of `2^k` half-cells on a side — the highest top plane
// in the block, the smallest Z — packed the same way from texel row
// `losLevelRowOffset(k)`. The march steps over a block whose highest top
// cannot lower its result. The CPU field is that whole image.
constexpr int kFogLosLevelCount = 8;
constexpr int losLevelRowOffset(int level) {
    return 2 * kFogLosTextureSize - ((2 * kFogLosTextureSize) >> level);
}
constexpr int kFogLosTextureHeight = losLevelRowOffset(kFogLosLevelCount);
constexpr std::size_t kFogLosColumnCount =
    static_cast<std::size_t>(kFogLosFieldSize) * static_cast<std::size_t>(kFogLosFieldSize);
constexpr std::size_t kFogLosFieldFloatCount = static_cast<std::size_t>(kFogLosTextureSize) *
                                               static_cast<std::size_t>(kFogLosTextureHeight) * 4u;
// Added to a half-cell index before it is shifted down to a block index, so
// the shift never sees a negative value; a multiple of every block size.
constexpr int kFogLosLevelBias = 1 << 16;
constexpr float kFogLosColumnEmpty = std::numeric_limits<float>::max();
// Below this signed clearance (world units, positive = the segment passes
// above the column top) a column hides the sample. A sample resting exactly
// on its own column's top plane reads a clearance of 0 up to float rounding.
constexpr float kFogLosClearanceTolerance = 1.0e-3f;
static_assert(
    kMaxFogVisionCircles == 8, "losParams_ mirrors two vec4 lanes per field in the shaders"
);

// Read-only view over a published line-of-sight column field
// (`C_CanvasFogOfWar::losField`) and the half-cell at its lower corner. A
// default-constructed view is unpublished: every gated source reads occluded,
// so nothing is revealed through a field that has never been built. The
// default corner centres the field on the world origin, where a window
// centred there anchors it.
struct FogLosColumnField {
    const float *tops_ = nullptr;
    IRMath::ivec2 fieldMin_{-kFogLosFieldHalfExtent, -kFogLosFieldHalfExtent};

    /// The lower-corner half-cell of the field anchored with a fog window at
    /// @p windowOrigin of edge @p windowEdge: the window's centre less the
    /// field's half extent, snapped down to a block of the coarsest pyramid
    /// level so every block keeps its world-aligned corner. Mirrors
    /// `fogLosFieldMin` in the shader twins.
    static IRMath::ivec2 fieldMinForWindow(IRMath::ivec2 windowOrigin, int windowEdge) {
        const IRMath::ivec2 corner =
            (windowOrigin + windowEdge / 2) * kFogLosCellsPerUnit - kFogLosFieldHalfExtent;
        return IRMath::ivec2(
            blockMin(kFogLosLevelCount - 1, corner.x),
            blockMin(kFogLosLevelCount - 1, corner.y)
        );
    }

    static bool cellInField(IRMath::ivec2 halfCell, IRMath::ivec2 fieldMin) {
        const IRMath::ivec2 local = halfCell - fieldMin;
        return local.x >= 0 && local.x < kFogLosFieldSize && local.y >= 0 &&
               local.y < kFogLosFieldSize;
    }

    /// The half-cell containing world coordinate @p worldXY: half-cell `h`
    /// spans `[h / 2, (h + 1) / 2)`.
    static int halfCellOf(float world) {
        return static_cast<int>(IRMath::floor(world * static_cast<float>(kFogLosCellsPerUnit)));
    }

    /// Flat float index of block @p (blockX, blockY) of pyramid level
    /// @p level — level 0's blocks are the half-cells — counted from the
    /// field's corner, in the packed image `losTexture_` uploads verbatim.
    static std::size_t blockIndex(int level, std::size_t blockX, std::size_t blockY) {
        const std::size_t texel =
            (static_cast<std::size_t>(losLevelRowOffset(level)) + blockY / 2u) *
                static_cast<std::size_t>(kFogLosTextureSize) +
            blockX / 2u;
        return texel * 4u + (blockX & 1u) + 2u * (blockY & 1u);
    }

    /// Flat float index of in-field half-cell @p halfCell of a field whose
    /// lower corner is @p fieldMin.
    static std::size_t columnIndex(IRMath::ivec2 halfCell, IRMath::ivec2 fieldMin) {
        const IRMath::ivec2 local = halfCell - fieldMin;
        return blockIndex(0, static_cast<std::size_t>(local.x), static_cast<std::size_t>(local.y));
    }

    /// The lower corner, in half-cells, of the level-@p level block holding
    /// half-cell @p halfCell along one axis.
    static int blockMin(int level, int halfCell) {
        return (((halfCell + kFogLosLevelBias) >> level) << level) - kFogLosLevelBias;
    }

    bool published() const {
        return tops_ != nullptr;
    }

    /// The top plane of half-cell @p (halfCellX, halfCellY); empty outside
    /// the field.
    float topPlane(int halfCellX, int halfCellY) const {
        const IRMath::ivec2 halfCell(halfCellX, halfCellY);
        if (!cellInField(halfCell, fieldMin_))
            return kFogLosColumnEmpty;
        return tops_[columnIndex(halfCell, fieldMin_)];
    }

    /// The highest top plane (the smallest Z) among the half-cells of the
    /// level-@p level block whose lower corner is @p (blockMinX, blockMinY);
    /// empty outside the field. Level 0 is `topPlane`.
    float blockTop(int level, int blockMinX, int blockMinY) const {
        const IRMath::ivec2 local = IRMath::ivec2(blockMinX, blockMinY) - fieldMin_;
        if (local.x < 0 || local.x >= kFogLosFieldSize || local.y < 0 ||
            local.y >= kFogLosFieldSize)
            return kFogLosColumnEmpty;
        return tops_[blockIndex(
            level,
            static_cast<std::size_t>(local.x) >> level,
            static_cast<std::size_t>(local.y) >> level
        )];
    }
};

// GPU UBO payload for the analytic vision circles (binding
// `kBufferIndex_FogObservers`). Held directly on the component as the upload
// source of truth — the system uploads it verbatim each frame, so the
// std140 / Metal layout must match `FogObserverData` in the consuming
// shaders exactly. `visionCircles_[i]` = (centerX, centerY, radius,
// edgeSoftness) in world units; only the first `visionCircleCount_` entries
// are read.
struct FrameDataFogObservers {
    IRMath::vec4 visionCircles_[kMaxFogVisionCircles] = {};
    std::int32_t visionCircleCount_ = 0;
    /// Bit `i` set = source `i` is gated by line of sight. Read by the fog
    /// pass only; every other `FogObserverData` mirror spells the lane out at
    /// the same offset.
    std::int32_t losSourceMask_ = 0;
    /// The field column at texel (0, 0) of the fog window texture the same
    /// frame uploads (`C_CanvasFogOfWar::windowOrigin_`), written by the
    /// gather before any observer upload so the taps and the texture agree.
    /// Every shader that taps the grid declares both lanes; the fog pass also
    /// anchors the line-of-sight field on them.
    std::int32_t windowOriginX_ = 0;
    std::int32_t windowOriginY_ = 0;
    /// Per-circle height penalty, std140-appended after the tail so
    /// every preceding member offset is unchanged. A shader that reads only
    /// `visionCircles_` / `visionCircleCount_` (c_voxel_to_trixel_stage_2,
    /// c_voxel_visibility_compact) sees byte-identical bytes and needs no edit.
    /// `visionCircleHeights_[i]` = (observerZ, zCostUp, zCostDown, freeBand).
    /// The
    /// fog reveal adds `zCostUp * max(dzUp - freeBand, 0) + zCostDown *
    /// max(dzDown - freeBand, 0)` to the radial XY distance, where `dzUp =
    /// max(observerZ - z, 0)` and `dzDown = max(z - observerZ, 0)` (iso +Z is
    /// the downward height axis), so matter far above/below the observer's
    /// height reveals less at the same XY, asymmetrically and with a
    /// penalty-free band around the observer's height. All-zero (the default
    /// for every existing caller) makes the penalty term exactly 0, so the
    /// height term is inert for existing callers.
    /// Only the first `visionCircleCount_` entries are read, paired 1:1 with
    /// `visionCircles_`.
    IRMath::vec4 visionCircleHeights_[kMaxFogVisionCircles] = {};
    /// RGBA the fog pass paints fully unexplored matter with — the state-0
    /// anchor of its two-segment lerp. Appended after `visionCircleHeights_`
    /// so every earlier offset is unchanged; only `ir_fog_common` declares
    /// it. Alpha is unused (the pass preserves the source alpha).
    IRMath::vec4 unexploredColor_ = IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    /// Per-source line of sight, `losParams_[i]` = (losEyeHeight, losSoftness,
    /// 0, 0): the eye's height above `observerZ` (`kFogVisionLosOff` while
    /// the source is ungated) and the clearance band the gate grades over
    /// (`kFogLosHardGate` = a step). Read only for sources in `losSourceMask_`
    /// by the fog passes, which alone declare this tail.
    IRMath::vec4 losParams_[kMaxFogVisionCircles] = {};

    float losEyeHeight(int source) const {
        return losParams_[source].x;
    }

    float losSoftness(int source) const {
        return losParams_[source].y;
    }

    bool losGated(int source) const {
        return ((losSourceMask_ >> source) & 1) != 0;
    }
};
static_assert(
    sizeof(FrameDataFogObservers) == 3 * kMaxFogVisionCircles * 16 + 16 + 16,
    "FrameDataFogObservers must stay std140/Metal-tight (vec4[N] + ivec4 tail + vec4[N] + vec4 + "
    "vec4[N])"
);

struct C_CanvasFogOfWar {
    std::pair<ResourceId, Texture2D *> texture_;
    std::shared_ptr<IRPrefab::Fog::WorldField> field_;
    /// Edge of the square window texture, fixed at construction from the
    /// canvas the fog is attached to.
    int windowEdge_ = 0;
    /// Field column at texel (0, 0) of the window the texture currently
    /// shows (the toroidal address of that column); unset until the first
    /// gather and after `clearAll` or an accepted persistence root, which
    /// makes the next gather re-expand the whole window.
    std::optional<IRMath::ivec2> windowOrigin_;
    /// Live analytic vision circles (the smooth, sub-voxel reveal). This is
    /// the upload payload the system pushes to the `kBufferIndex_FogObservers`
    /// UBO verbatim every frame — small and unconditional, so unlike the grid
    /// texture it needs no dirty flag. Cleared/added via `clearVisionCircles`
    /// / `addVisionCircle`; empty (count 0) means grid-only.
    FrameDataFogObservers observers_{};
    /// Line-of-sight column field (layout in the header comment). The CPU
    /// mirror is the texel image itself; `FOG_LOS_BUILD` writes both.
    /// `losQueryColumnTops_` is `lineOfSight`'s own, sized on its first call.
    std::pair<ResourceId, Texture2D *> losTexture_;
    std::vector<float> losColumnTops_;
    std::vector<float> losQueryColumnTops_;
    /// The observers `losColumnTops_` was built for, published together with
    /// it: a CPU consumer of the field reads source slots from here, never from
    /// the live `observers_`, so a slot re-authored after the build cannot pair
    /// with another frame's columns.
    FrameDataFogObservers losPublishedObservers_{};
    /// The lower-corner half-cell `losColumnTops_` was built at, published
    /// with it.
    IRMath::ivec2 losPublishedFieldMin_{-kFogLosFieldHalfExtent, -kFogLosFieldHalfExtent};
    bool losPublished_ = false;

    /// Sizes the window for the main canvas; `attachToCanvas` passes the
    /// target canvas's own size.
    C_CanvasFogOfWar()
        : C_CanvasFogOfWar(IRMath::ivec2(IRRender::getMainCanvasSizeTrixels())) {}

    /// A window sized for a fog canvas of @p canvasSize trixels
    /// (`IRPrefab::Fog::detail::windowEdgeForCanvas`).
    explicit C_CanvasFogOfWar(IRMath::ivec2 canvasSize)
        : texture_{IRRender::createResource<IRRender::Texture2D>(
              TextureKind::TEXTURE_2D,
              IRPrefab::Fog::detail::windowEdgeForCanvas(canvasSize),
              IRPrefab::Fog::detail::windowEdgeForCanvas(canvasSize),
              TextureFormat::RGBA8,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::NEAREST
          )}
        , field_{std::make_shared<IRPrefab::Fog::WorldField>()}
        , windowEdge_{IRPrefab::Fog::detail::windowEdgeForCanvas(canvasSize)}
        , losTexture_{IRRender::createResource<IRRender::Texture2D>(
              TextureKind::TEXTURE_2D,
              kFogLosTextureSize,
              kFogLosTextureHeight,
              TextureFormat::RGBA32F,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::NEAREST
          )}
        , losColumnTops_(kFogLosFieldFloatCount, kFogLosColumnEmpty) {
        // The shader reads this texture only for gated sources, and a gated
        // frame uploads it whole first; the empty seed keeps a creation that
        // gates a source without registering FOG_LOS_BUILD unoccluded.
        const float emptyTexel[4] =
            {kFogLosColumnEmpty, kFogLosColumnEmpty, kFogLosColumnEmpty, kFogLosColumnEmpty};
        losTexture_.second->clear(PixelDataFormat::RGBA, PixelDataType::FLOAT32, emptyTexel);
    }

    // Tag selecting the textureless constructor.
    struct HeadlessInit {};

    // The CPU field and observers with no GPU texture, for a headless test
    // that drives the reveal systems without a render manager. A render
    // system reaching `getTexture()` on this instance asserts.
    explicit C_CanvasFogOfWar(HeadlessInit)
        : texture_{0, nullptr}
        , field_{std::make_shared<IRPrefab::Fog::WorldField>()}
        , losTexture_{0, nullptr}
        , losColumnTops_(kFogLosFieldFloatCount, kFogLosColumnEmpty) {}

    void onDestroy() {
        if (texture_.second != nullptr) {
            IRRender::destroyResource<Texture2D>(texture_.first);
        }
        if (losTexture_.second != nullptr) {
            IRRender::destroyResource<Texture2D>(losTexture_.first);
        }
        field_.reset();
    }

    Texture2D *getLosTexture() const {
        return losTexture_.second;
    }

    /// The published column view at the corner it was built at; unpublished
    /// until `FOG_LOS_BUILD` has run with a gated source.
    FogLosColumnField losField() const {
        return FogLosColumnField{
            losPublished_ ? losColumnTops_.data() : nullptr,
            losPublishedFieldMin_
        };
    }

    /// The field corner anchored with the window the live observer lanes
    /// name — the window the last gather uploaded.
    IRMath::ivec2 losFieldMinForLiveWindow() const {
        return FogLosColumnField::fieldMinForWindow(
            IRMath::ivec2(observers_.windowOriginX_, observers_.windowOriginY_),
            windowEdge_
        );
    }

    Texture2D *getTexture() const {
        IR_ASSERT(
            texture_.second != nullptr,
            "C_CanvasFogOfWar::getTexture() called on default-"
            "constructed instance — must be constructed via the "
            "default ctor (which allocates the GPU texture)."
        );
        return texture_.second;
    }

    std::uint8_t getCell(int wx, int wy) const {
        return field_->getCell({wx, wy});
    }

    /// The resident state of (wx, wy), an absent chunk reading unexplored.
    /// Never loads and never sets an access bit, so a `PARALLEL_FOR` tick may
    /// read it while no serial phase runs (fog-of-war-world-field.md D13).
    std::uint8_t peekCell(int wx, int wy) const {
        return field_->peekCell({wx, wy}).value_or(kFogStateUnexplored);
    }

    /// Makes (wx, wy)'s region resident for a later `peekCell`; serial only.
    void touchCell(int wx, int wy) {
        field_->touchCell({wx, wy});
    }

    bool hasPersistence() const {
        return field_->hasPersistence();
    }

    void setCell(int wx, int wy, std::uint8_t state) {
        field_->setCell({wx, wy}, state);
    }

    /// Mark every cell within `radius` (Euclidean distance, clamped to
    /// `IRPrefab::Fog::kFogRevealRadiusMax`) of `(cx,cy)` as visible. Cells
    /// previously visible but now outside the radius are NOT downgraded —
    /// that lifecycle belongs to the deferred `fadeExplored` pass, since
    /// downgrade requires knowing every vision source's union
    /// (game-state-specific). v1 callers that want a single moving observer
    /// can wipe the field themselves before each `revealRadius` call.
    void revealRadius(int cx, int cy, int radius) {
        field_->revealRadius({cx, cy}, radius);
    }

    /// Drop all live vision sources, analytic and field-tier, returning to
    /// grid-only fog. A single moving observer calls this then
    /// `addVisionCircle` each frame — and, for a line-of-sight source,
    /// `setVisionCircleLineOfSight` again, since every new slot starts with
    /// LOS off.
    void clearVisionCircles() {
        clearVisionCircles(observers_, *field_);
    }

    /// Add a live analytic vision disc centered at the (fractional) world
    /// column @p (cx,cy) with @p radius (world units). The fog shader reveals
    /// it per pixel from the continuous world position, so the edge is crisp at
    /// render resolution and tracks sub-voxel motion without grid quantization
    /// — no grid write, no texture upload. @p edge is the edge softness in
    /// world units (default `kFogVisionEdgeDefault` reads as antialiasing;
    /// larger = a deliberately soft falloff). Multiple circles compose via
    /// `max` in the shader; dropped for a non-positive radius. Unlike
    /// `revealRadius`, an analytic source touches NO grid cell — to also leave
    /// explored "memory" behind a moving observer, stamp the grid separately
    /// (e.g. integer `revealRadius` for the voxelized floor).
    ///
    /// Tiers. The first `kMaxFogVisionCircles` sources since the last
    /// `clearVisionCircles`, in call order, are analytic; add the
    /// highest-priority sources first. Every later source is field-tier: its
    /// XY disc (cells whose centres lie within @p radius of
    /// `roundHalfUp(cx, cy)`, the `revealRadius` metric) is stamped visible
    /// into the world field's transient layer until `clearVisionCircles`. The
    /// tier ignores @p edge, the height terms, line of sight and channels, and
    /// leaves no explored memory; it is never persisted. `getCell` reads it
    /// (so a cell under a tier disc reads visible even after `setCell` wrote
    /// it lower), and it reaches the screen only inside the fog window — a
    /// tier disc off the window changes nothing drawn but still reads visible
    /// through `getCell`.
    ///
    /// @p observerZ + @p zCostUp + @p zCostDown + @p freeBand shape the disc
    /// into an XY radius with an
    /// asymmetric, penalty-free-banded height penalty: the effective reveal
    /// distance is `dist_xy + zCostUp * max(dzUp - freeBand, 0) + zCostDown *
    /// max(dzDown - freeBand, 0)`, where `dzUp = max(observerZ - z, 0)`
    /// (above the observer; iso +Z is the downward height axis) and
    /// `dzDown = max(z - observerZ, 0)` (below). Matter within @p freeBand
    /// world units of the observer's height reveals to the full radius; past
    /// the band, a tall pillar top and a deep pit floor at the same XY fade at
    /// independent rates. @p zCostDown < 0 (the default,
    /// `kFogVisionZCostMirrorUp`) mirrors @p zCostUp. All-defaults (@p
    /// zCostUp 0, @p freeBand 0) is the back-compat plain 2D disc —
    /// byte-identical to the reveal without vertical-cost weighting.
    ///
    /// Returns the analytic slot the circle took — the index
    /// `setVisionCircleLineOfSight` takes — or -1 when it took none (dropped,
    /// or field-tier). The slot starts with LOS off.
    int addVisionCircle(
        float cx,
        float cy,
        float radius,
        float edge = kFogVisionEdgeDefault,
        float observerZ = 0.0f,
        float zCostUp = 0.0f,
        float zCostDown = kFogVisionZCostMirrorUp,
        float freeBand = 0.0f
    ) {
        return addVisionCircle(
            observers_,
            *field_,
            cx,
            cy,
            radius,
            edge,
            observerZ,
            zCostUp,
            zCostDown,
            freeBand
        );
    }

    /// Gate registered source @p source by line of sight with its eye
    /// @p losEyeHeight world units above its `observerZ` (the header comment
    /// has the model); a negative height (`kFogVisionLosOff`) ungates it.
    /// @p losSoftness > 0 grades the verdict over that many world units of
    /// clearance above an occluder's top; `kFogLosHardGate` (the default)
    /// keeps a step. @p source must name a registered slot — use
    /// `addVisionCircle`'s return. The eye's own column never occludes, but
    /// an ungoverned observer body spanning several columns does unless the
    /// eye clears its top; a governed body
    /// (`IRPrefab::Fog::setEntityRevealGoverned`) never occludes. A gated
    /// source needs `FOG_LOS_BUILD` in the RENDER pipeline.
    void setVisionCircleLineOfSight(
        int source, float losEyeHeight, float losSoftness = kFogLosHardGate
    ) {
        setVisionCircleLineOfSight(observers_, source, losEyeHeight, losSoftness);
    }

    /// The slot-authoring rules the members above apply to this component's
    /// `observers_`, on any payload. These field-free overloads author the
    /// analytic slots only: past the cap they drop.
    static void clearVisionCircles(FrameDataFogObservers &observers) {
        observers.visionCircleCount_ = 0;
        observers.losSourceMask_ = 0;
        for (IRMath::vec4 &params : observers.losParams_) {
            params = IRMath::vec4(kFogVisionLosOff, kFogLosHardGate, 0.0f, 0.0f);
        }
    }

    /// The members' full rules on any observers / field pair: the analytic
    /// slots plus @p field's transient tier layer. GPU-free.
    static void
    clearVisionCircles(FrameDataFogObservers &observers, IRPrefab::Fog::WorldField &field) {
        clearVisionCircles(observers);
        field.clearTransient();
    }

    static int addVisionCircle(
        FrameDataFogObservers &observers,
        IRPrefab::Fog::WorldField &field,
        float cx,
        float cy,
        float radius,
        float edge,
        float observerZ,
        float zCostUp,
        float zCostDown,
        float freeBand
    ) {
        if (radius > 0.0f && observers.visionCircleCount_ >= kMaxFogVisionCircles) {
            field.stampTransientDisc(IRMath::vec2(cx, cy), radius);
            return -1;
        }
        return addVisionCircle(
            observers,
            cx,
            cy,
            radius,
            edge,
            observerZ,
            zCostUp,
            zCostDown,
            freeBand
        );
    }

    static int addVisionCircle(
        FrameDataFogObservers &observers,
        float cx,
        float cy,
        float radius,
        float edge,
        float observerZ,
        float zCostUp,
        float zCostDown,
        float freeBand
    ) {
        if (radius <= 0.0f || observers.visionCircleCount_ >= kMaxFogVisionCircles)
            return -1;
        const int slot = observers.visionCircleCount_;
        observers.losSourceMask_ &= ~(1 << slot);
        observers.losParams_[slot] = IRMath::vec4(kFogVisionLosOff, kFogLosHardGate, 0.0f, 0.0f);
        observers.visionCircles_[observers.visionCircleCount_] =
            IRMath::vec4(cx, cy, radius, IRMath::max(edge, 0.0f));
        // Sentinel BEFORE clamp: zCostDown < 0 means "mirror zCostUp",
        // resolved against the already-clamped up-cost. Clamping first would
        // turn every mirror request into zCostDown = 0 (downhill free
        // everywhere) for every default caller — get the order right.
        const float clampedUp = IRMath::max(zCostUp, 0.0f);
        const float resolvedDown = zCostDown < 0.0f ? clampedUp : IRMath::max(zCostDown, 0.0f);
        // The two COST clamps are load-bearing, not defensive hygiene: they
        // are what keeps c_voxel_visibility_compact's z-FREE coarse cull a
        // superset of stage 1's z-AWARE detached-canvas drop, and FOG_TO_TRIXEL's
        // per-pixel reveal never brighter than the z-free one. That holds because
        // both penalty terms are products of clamped->=0 costs with
        // max(.,0) >= 0, so `distEff >= dist_xy` always, the penalized reveal
        // is pointwise <= the z-free one, and the drop can only ever drop
        // MORE. A negative COST inverts it, and the compact pass culls voxels
        // stage 1 would still render — matter silently missing, with no
        // shader error.
        // The freeBand clamp carries NONE of that: freeBand only subtracts
        // INSIDE max(.,0), so a negative band just makes the term
        // `dz + |freeBand|` — larger, never negative — and `distEff >=
        // dist_xy` survives an unclamped band intact. Its clamp is plain
        // hygiene (a negative penalty-free band is meaningless as a
        // semantic); it is the one clamp here that can be relaxed without
        // re-deriving the cull argument. `observers_` is public, so a caller
        // that writes visionCircleHeights_ directly owns this invariant
        // itself.
        observers.visionCircleHeights_[observers.visionCircleCount_] =
            IRMath::vec4(observerZ, clampedUp, resolvedDown, IRMath::max(freeBand, 0.0f));
        ++observers.visionCircleCount_;
        return slot;
    }

    static void setVisionCircleLineOfSight(
        FrameDataFogObservers &observers,
        int source,
        float losEyeHeight,
        float losSoftness = kFogLosHardGate
    ) {
        IR_ASSERT(
            source >= 0 && source < observers.visionCircleCount_,
            "setVisionCircleLineOfSight: source {} is not a registered vision circle (count {})",
            source,
            observers.visionCircleCount_
        );
        if (source < 0 || source >= observers.visionCircleCount_)
            return;
        if (losEyeHeight >= 0.0f) {
            observers.losSourceMask_ |= 1 << source;
            observers.losParams_[source] =
                IRMath::vec4(losEyeHeight, IRMath::max(losSoftness, kFogLosHardGate), 0.0f, 0.0f);
        } else {
            observers.losSourceMask_ &= ~(1 << source);
            observers.losParams_[source] =
                IRMath::vec4(kFogVisionLosOff, kFogLosHardGate, 0.0f, 0.0f);
        }
    }

    void clearAll() {
        field_->clear();
        windowOrigin_.reset();
    }
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_FOG_OF_WAR_H */
