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
// The CPU mirror's dirty flag gates `subImage2D`. VOXEL_TO_TRIXEL_STAGE_1
// performs the upload before using fog to cull unexplored columns;
// FOG_TO_TRIXEL is a read-only consumer. This is the documented exception to the
// "no dirty flags on components" rule —
// see `.claude/rules/cpp-ecs.md` § "No dirty flags on components".
// The texture is CPU-authored and GPU-read-only; per-cell uploads would split
// `revealRadius` into hundreds of API calls. Population is driver-side: gameplay calls
// `IRPrefab::Fog::setCell` / `IRPrefab::Fog::revealRadius` (see
// `render/fog_of_war.hpp`) to drive the visibility set directly.
//
// Sized to match the light-occlusion SSBO's 256×256 footprint on the ground
// plane (256 KiB CPU+GPU) and using the same `[-halfExtent, +halfExtent)`
// world-centered cell convention. One cell per integer voxel column.
// Out-of-range writes are silently dropped; out-of-range reads return
// `kFogStateUnexplored`. Out-of-range pixels in the shader are treated
// as visible via an explicit bounds check (image bindings bypass sampler
// wrap modes).
//
// `kFogOfWarSize` / `kFogOfWarHalfExtent` are mirrored as literals in the
// `ir_fog_common.{glsl,metal}` and `ir_fog_los.{glsl,metal}` include pairs.
// Renaming the C++ constants requires editing all four shader files in
// lockstep.
//
// Line of sight. A vision circle opted in with `setVisionCircleLineOfSight`
// reveals only what its eye can see over a 2.5D column model:
//   * Occluders: the active grid canvas's pool voxels with alpha > 0 and no
//     `VoxelReserved::kFogWholeBodyExempt` (a governed body never occludes),
//     plus every `C_ShapeDescriptor + C_LightBlocker{blocksLOS_} +
//     C_WorldTransform` shape on that canvas. A column is opaque downward from
//     its highest occupied voxel centre `T(c)` (the smallest Z; +Z is down).
//     Overhangs, caves and detached canvases are out of the model.
//   * Eye: `(cx, cy, observerZ - losEyeHeight)`, continuous.
//   * Horizon: for a target cell `t`, walk the supercover of the XY segment
//     from the eye to `t`'s centre, excluding the eye's cell and `t` (both
//     side cells of an exact corner tie are visited). Over the occupied columns
//     `c` on the walk, `H(t) = min[E.z + (T(c) - E.z) * d(t) / d(c)]`, with `d`
//     the XY distance from the eye to a cell centre; no occupied column means
//     clear.
//   * Gate: a sample is visible iff `roundHalfUp(z) <= H(roundHalfUp(xy))` —
//     an integer voxel-centre height against the stored float, identical in
//     the shader (`surfaceVoxel`) and the CPU oracle. Distance and height cost
//     keep reading the continuous position. This is the hard gate, the default
//     (`kFogLosHardGate`).
//   * Smooth gate (a source's `losSoftness` s >= 0): each cell's verdict is
//     `t = 1 - smoothstep(H, H + s, z)` (a step at s = 0; clear and out-of-field
//     cells read 1), and the source's visibility `v` blends the four cell
//     centres around the sample's continuous XY bilinearly at the rounded
//     voxel height the hard gate compares, so a cell centre reads its own
//     verdict and the ramp between cells is one cell wide. A vertical-face
//     pixel instead takes the single cell one step along its face's outward
//     normal from the voxel it belongs to (recovered from the pixel's depth
//     encoding, rounded as the column build rounds it), at that voxel's height:
//     a face is gated by the space it faces. `v` scales the source's
//     reveal, and its rim distance past the radius eases toward the fade width
//     (no lift) as `v` falls. The CPU oracle (`FogLineOfSightField::visibility`)
//     evaluates the top-face rule at a point; entities have no face. Limit: a
//     taller clear-horizon column beside a shadowed cell lifts that cell's
//     samples toward 1/2 at the shared edge, falling to its own verdict at its
//     centre; the hard gate steps at the same edge.
// `FOG_LOS_BUILD` rebuilds the columns and every gated source's horizons each
// RENDER frame and uploads `losTexture_` (256 × 512 RGBA32F: source `i` at
// tile row `i / 4`, channel `i % 4`; `kFogLosHorizonClear` = unoccluded, the
// value outside the disc and outside the footprint). Cells outside the fog
// footprint are clear, and occluders outside it are unknown.

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/texture.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

using namespace IRMath;
using namespace IRRender;

namespace IRComponents {

constexpr int kFogOfWarSize = 256;
constexpr int kFogOfWarHalfExtent = kFogOfWarSize / 2;

constexpr std::uint8_t kFogStateUnexplored = 0;
constexpr std::uint8_t kFogStateExplored = 128;
constexpr std::uint8_t kFogStateVisible = 255;

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
// past that, callers fall back to the grid.
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
// `setVisionCircleLineOfSight` softness that selects the hard gate; any value
// >= 0 selects the smooth gate with that band width in voxels.
constexpr float kFogLosHardGate = -1.0f;
using FogLosEyeHeights = std::array<float, kMaxFogVisionCircles>;

constexpr int kFogLosSourcesPerTile = 4;
constexpr int kFogLosTextureWidth = kFogOfWarSize;
constexpr int kFogLosTextureHeight = kFogOfWarSize * (kMaxFogVisionCircles / kFogLosSourcesPerTile);
static_assert(
    kMaxFogVisionCircles % kFogLosSourcesPerTile == 0, "LOS tiles pack four sources per RGBA texel"
);
constexpr std::size_t kFogLosHorizonCount = static_cast<std::size_t>(kFogLosTextureWidth) *
                                            static_cast<std::size_t>(kFogLosTextureHeight) * 4u;
constexpr std::size_t kFogLosColumnCount =
    static_cast<std::size_t>(kFogOfWarSize) * static_cast<std::size_t>(kFogOfWarSize);
constexpr float kFogLosHorizonClear = std::numeric_limits<float>::max();
constexpr std::int32_t kFogLosColumnEmpty = std::numeric_limits<std::int32_t>::max();

// Read-only view over a published LOS horizon image (`C_CanvasFogOfWar::losField`).
// A default-constructed view is unpublished: every gated source reads occluded,
// so nothing is revealed through a field that has never been built.
struct FogLineOfSightField {
    const float *horizons_ = nullptr;

    static bool cellInField(int cellX, int cellY) {
        return cellX >= -kFogOfWarHalfExtent && cellX < kFogOfWarHalfExtent &&
               cellY >= -kFogOfWarHalfExtent && cellY < kFogOfWarHalfExtent;
    }

    /// Flat float index of source @p source's horizon at in-field cell
    /// @p (cellX, cellY) — the texel layout `losTexture_` uploads verbatim.
    static std::size_t horizonIndex(int source, int cellX, int cellY) {
        const std::size_t x = static_cast<std::size_t>(cellX + kFogOfWarHalfExtent);
        const std::size_t y = static_cast<std::size_t>(
            cellY + kFogOfWarHalfExtent + (source / kFogLosSourcesPerTile) * kFogOfWarSize
        );
        const std::size_t channel = static_cast<std::size_t>(source % kFogLosSourcesPerTile);
        return (y * static_cast<std::size_t>(kFogLosTextureWidth) + x) * 4u + channel;
    }

    bool published() const {
        return horizons_ != nullptr;
    }

    /// The gate for source @p source at sample voxel @p sample (the rounded
    /// sample position). Out-of-field cells are visible.
    bool visible(int source, IRMath::ivec3 sample) const {
        if (horizons_ == nullptr)
            return false;
        if (!cellInField(sample.x, sample.y))
            return true;
        return static_cast<float>(sample.z) <= horizons_[horizonIndex(source, sample.x, sample.y)];
    }

    /// One cell's smooth verdict at height @p z: 1 at or below its horizon,
    /// 0 above it at @p softness 0, else the smoothstep band. Mirrors
    /// `fogLosTapVerdict`; out-of-field cells read 1.
    float cellVerdict(int source, int cellX, int cellY, float z, float softness) const {
        if (!cellInField(cellX, cellY))
            return 1.0f;
        const float horizon = horizons_[horizonIndex(source, cellX, cellY)];
        if (z <= horizon)
            return 1.0f;
        if (softness <= 0.0f)
            return 0.0f;
        const float t = IRMath::clamp((z - horizon) / softness, 0.0f, 1.0f);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }

    /// The smooth gate for source @p source at @p position with band
    /// @p softness (>= 0): the four cell centres around its continuous XY
    /// blended bilinearly at its rounded voxel height (the header comment has
    /// the rule). 0 for an unpublished field.
    float visibility(int source, IRMath::vec3 position, float softness) const {
        if (horizons_ == nullptr)
            return 0.0f;
        const float z = static_cast<float>(IRMath::roundHalfUp(position.z));
        const float baseX = IRMath::floor(position.x);
        const float baseY = IRMath::floor(position.y);
        const float wx = position.x - baseX;
        const float wy = position.y - baseY;
        const int x = static_cast<int>(baseX);
        const int y = static_cast<int>(baseY);
        const float v00 = cellVerdict(source, x, y, z, softness);
        const float v10 = cellVerdict(source, x + 1, y, z, softness);
        const float v01 = cellVerdict(source, x, y + 1, z, softness);
        const float v11 = cellVerdict(source, x + 1, y + 1, z, softness);
        return IRMath::mix(IRMath::mix(v00, v10, wx), IRMath::mix(v01, v11, wx), wy);
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
    /// shaders only; the other `FogObserverData` mirrors keep this lane as
    /// unread padding at the same offset.
    std::int32_t losSourceMask_ = 0;
    std::int32_t pad1_ = 0;
    std::int32_t pad2_ = 0;
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
    /// Per-source line-of-sight softness, source `i` at `[i / 4][i % 4]`:
    /// `kFogLosHardGate` (any negative) selects the hard gate, >= 0 the smooth
    /// gate with that band in voxels. Read only for sources in
    /// `losSourceMask_`; appended last, and only the fog passes declare it.
    IRMath::vec4 losSoftness_[kMaxFogVisionCircles / 4] = {
        IRMath::vec4(kFogLosHardGate), IRMath::vec4(kFogLosHardGate)
    };

    float losSoftness(int source) const {
        return losSoftness_[source / 4][source % 4];
    }

    void setLosSoftness(int source, float softness) {
        losSoftness_[source / 4][source % 4] = softness;
    }

    /// True when a live source reads the smooth gate: FOG_TO_TRIXEL dispatches
    /// the smooth kernel variant only then, so the hard variant's reveal loop
    /// carries no smooth-gate code.
    bool hasSmoothLineOfSightSource() const {
        for (int source = 0; source < visionCircleCount_; ++source) {
            if ((losSourceMask_ & (1 << source)) != 0 && losSoftness(source) >= 0.0f) {
                return true;
            }
        }
        return false;
    }
};
static_assert(
    sizeof(FrameDataFogObservers) ==
        2 * kMaxFogVisionCircles * 16 + 16 + 16 + kMaxFogVisionCircles * 4,
    "FrameDataFogObservers must stay std140/Metal-tight "
    "(vec4[N] + ivec4 tail + vec4[N] + vec4 + vec4[N / 4])"
);
static_assert(kMaxFogVisionCircles == 8, "losSoftness_ initializes two vec4 lanes");

struct C_CanvasFogOfWar {
    std::pair<ResourceId, Texture2D *> texture_;
    /// CPU mirror of the .r channel of the GPU texture. Writes go here
    /// first; the system expands to RGBA on upload when `dirty_` is set.
    /// Held on the component (rather than re-allocated per frame) so
    /// writes stay allocation-free for the common per-frame
    /// `revealRadius` case.
    std::vector<std::uint8_t> cpuBuffer_;
    /// Set by any cell-mutating helper; cleared by VOXEL_TO_TRIXEL_STAGE_1
    /// after the `subImage2D` upload completes. Also set on construction so the
    /// initial all-zero state ships through to the GPU before the first
    /// FOG_TO_TRIXEL dispatch (a stale GPU-side texture from a previous
    /// frame's canvas teardown would otherwise leak into this canvas).
    bool dirty_ = true;
    /// Tracks whether every cell is `kFogStateUnexplored`. Set true by
    /// `clearAll()` after the fill; cleared false by `setCell()` and
    /// `revealRadius()` on first write. Lets `clearAll()` skip the O(N)
    /// scan and avoid the GPU upload when the buffer is already clean.
    bool allUnexplored_ = true;
    /// Live analytic vision circles (the smooth, sub-voxel reveal). This is
    /// the upload payload the system pushes to the `kBufferIndex_FogObservers`
    /// UBO verbatim every frame — small and unconditional, so unlike the grid
    /// texture it needs no dirty flag. Cleared/added via `clearVisionCircles`
    /// / `addVisionCircle`; empty (count 0) means grid-only.
    FrameDataFogObservers observers_{};
    /// Per-source eye height above `observerZ`; `kFogVisionLosOff` when the
    /// source is not gated. CPU-only: the shader reads built horizons.
    FogLosEyeHeights losEyeHeights_{};
    /// Line-of-sight horizon image (layout in the header comment). The CPU
    /// mirror is the texel image itself; `FOG_LOS_BUILD` writes both.
    std::pair<ResourceId, Texture2D *> losTexture_;
    std::vector<float> losHorizons_;
    /// Column tops `T(c)` in `flatIndex` order, `kFogLosColumnEmpty` for none.
    /// `FOG_LOS_BUILD` scratch; `losQueryColumnTops_` is `lineOfSight`'s own,
    /// sized on its first call.
    std::vector<std::int32_t> losColumnTops_;
    std::vector<std::int32_t> losQueryColumnTops_;
    /// The observers `losHorizons_` was built from, published together with
    /// it: a CPU consumer of the field reads source slots from here, never from
    /// the live `observers_`, so a slot re-authored after the build cannot pair
    /// with another source's horizons.
    FrameDataFogObservers losPublishedObservers_{};
    bool losPublished_ = false;

    C_CanvasFogOfWar()
        : texture_{IRRender::createResource<IRRender::Texture2D>(
              TextureKind::TEXTURE_2D,
              kFogOfWarSize,
              kFogOfWarSize,
              TextureFormat::RGBA8,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::NEAREST
          )}
        , cpuBuffer_(
              static_cast<std::size_t>(kFogOfWarSize) * static_cast<std::size_t>(kFogOfWarSize),
              kFogStateUnexplored
          )
        , losTexture_{IRRender::createResource<IRRender::Texture2D>(
              TextureKind::TEXTURE_2D,
              kFogLosTextureWidth,
              kFogLosTextureHeight,
              TextureFormat::RGBA32F,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::NEAREST
          )}
        , losHorizons_(kFogLosHorizonCount, kFogLosHorizonClear)
        , losColumnTops_(kFogLosColumnCount, kFogLosColumnEmpty) {
        losEyeHeights_.fill(kFogVisionLosOff);
        // The shader reads this texture only for gated sources, and a gated
        // frame uploads it whole first; the clear seed keeps a creation that
        // gates a source without registering FOG_LOS_BUILD unoccluded.
        const float clearTexel[4] =
            {kFogLosHorizonClear, kFogLosHorizonClear, kFogLosHorizonClear, kFogLosHorizonClear};
        losTexture_.second->clear(PixelDataFormat::RGBA, PixelDataType::FLOAT32, clearTexel);
    }

    void onDestroy() {
        IRRender::destroyResource<Texture2D>(texture_.first);
        IRRender::destroyResource<Texture2D>(losTexture_.first);
    }

    Texture2D *getLosTexture() const {
        return losTexture_.second;
    }

    /// The published horizon view; unpublished until `FOG_LOS_BUILD` has run
    /// with a gated source.
    FogLineOfSightField losField() const {
        return FogLineOfSightField{losPublished_ ? losHorizons_.data() : nullptr};
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

    static bool inBounds(int wx, int wy) {
        return wx >= -kFogOfWarHalfExtent && wx < kFogOfWarHalfExtent &&
               wy >= -kFogOfWarHalfExtent && wy < kFogOfWarHalfExtent;
    }

    static std::size_t flatIndex(int wx, int wy) {
        const std::size_t x = static_cast<std::size_t>(wx + kFogOfWarHalfExtent);
        const std::size_t y = static_cast<std::size_t>(wy + kFogOfWarHalfExtent);
        const std::size_t s = static_cast<std::size_t>(kFogOfWarSize);
        return y * s + x;
    }

    std::uint8_t getCell(int wx, int wy) const {
        if (!inBounds(wx, wy))
            return kFogStateUnexplored;
        return cpuBuffer_[flatIndex(wx, wy)];
    }

    void setCell(int wx, int wy, std::uint8_t state) {
        if (!inBounds(wx, wy))
            return;
        const std::size_t idx = flatIndex(wx, wy);
        if (cpuBuffer_[idx] == state)
            return;
        cpuBuffer_[idx] = state;
        dirty_ = true;
        if (state != kFogStateUnexplored)
            allUnexplored_ = false;
    }

    /// Mark every cell within `radius` (Euclidean distance) of `(cx,cy)`
    /// as visible. Cells previously visible but now outside the radius
    /// are NOT downgraded — that lifecycle belongs to the deferred
    /// `fadeExplored` pass, since downgrade requires knowing every
    /// vision source's union (game-state-specific). v1 callers that
    /// want a single moving observer can wipe the texture themselves
    /// before each `revealRadius` call.
    void revealRadius(int cx, int cy, int radius) {
        if (radius < 0)
            return;
        const int xMin = IRMath::max(cx - radius, -kFogOfWarHalfExtent);
        const int xMax = IRMath::min(cx + radius, kFogOfWarHalfExtent - 1);
        const int yMin = IRMath::max(cy - radius, -kFogOfWarHalfExtent);
        const int yMax = IRMath::min(cy + radius, kFogOfWarHalfExtent - 1);
        // The loop bounds already clamp iteration to the grid, so a radius
        // spanning more than the full grid extent reveals every in-bounds
        // cell regardless. Clamp before squaring so `radius * radius` can't
        // overflow int (UB above ~46340) for absurd inputs — `2 *
        // kFogOfWarSize` exceeds the largest squared cell distance any
        // in-bounds center can produce, so every in-range call stays
        // byte-identical, and the multiply is hoisted out of the inner loop.
        const int radiusClamped = IRMath::min(radius, 2 * kFogOfWarSize);
        const int radiusSq = radiusClamped * radiusClamped;
        for (int y = yMin; y <= yMax; ++y) {
            for (int x = xMin; x <= xMax; ++x) {
                const int dx = x - cx;
                const int dy = y - cy;
                if (dx * dx + dy * dy > radiusSq)
                    continue;
                const std::size_t idx = flatIndex(x, y);
                if (cpuBuffer_[idx] == kFogStateVisible)
                    continue;
                cpuBuffer_[idx] = kFogStateVisible;
                dirty_ = true;
                allUnexplored_ = false;
            }
        }
    }

    /// Drop all live vision circles, returning to grid-only fog. A
    /// single moving observer calls this then `addVisionCircle` each frame —
    /// and, for a line-of-sight source, `setVisionCircleLineOfSight` again,
    /// since every new slot starts with LOS off.
    void clearVisionCircles() {
        clearVisionCircles(observers_, losEyeHeights_);
    }

    /// Add a live analytic vision disc centered at the (fractional) world
    /// column @p (cx,cy) with @p radius (world units). The fog shader reveals
    /// it per pixel from the continuous world position, so the edge is crisp at
    /// render resolution and tracks sub-voxel motion without grid quantization
    /// — no grid write, no texture upload. @p edge is the edge softness in
    /// world units (default `kFogVisionEdgeDefault` reads as antialiasing;
    /// larger = a deliberately soft falloff). Multiple circles compose via
    /// `max` in the shader; silently dropped past `kMaxFogVisionCircles` or
    /// for a non-positive radius. Unlike `revealRadius`, this touches NO grid
    /// cell — to also leave explored "memory" behind a moving observer, stamp
    /// the grid separately (e.g. integer `revealRadius` for the voxelized
    /// floor).
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
    /// Returns the slot the circle took — the index `setVisionCircleLineOfSight`
    /// takes — or -1 when it was dropped. The slot starts with LOS off.
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
            losEyeHeights_,
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
    /// has the model); a negative height (`kFogVisionLosOff`) ungates it and
    /// resets its softness. @p losSoftness >= 0 selects the smooth gate with a
    /// band that many voxels wide; the default (`kFogLosHardGate`, any
    /// negative) keeps the hard gate. @p source must name a registered slot —
    /// use `addVisionCircle`'s return.
    /// The eye's own column never occludes, but an ungoverned observer body
    /// spanning several columns does unless the eye clears its top; a governed
    /// body (`IRPrefab::Fog::setEntityRevealGoverned`) never occludes. A gated
    /// source needs `FOG_LOS_BUILD` in the RENDER pipeline.
    void setVisionCircleLineOfSight(
        int source, float losEyeHeight, float losSoftness = kFogLosHardGate
    ) {
        setVisionCircleLineOfSight(observers_, losEyeHeights_, source, losEyeHeight, losSoftness);
    }

    /// The slot-authoring rules the members above apply to this component's
    /// `observers_` / `losEyeHeights_`, on any pair.
    static void clearVisionCircles(FrameDataFogObservers &observers, FogLosEyeHeights &eyeHeights) {
        observers.visionCircleCount_ = 0;
        observers.losSourceMask_ = 0;
        for (IRMath::vec4 &lane : observers.losSoftness_) {
            lane = IRMath::vec4(kFogLosHardGate);
        }
        eyeHeights.fill(kFogVisionLosOff);
    }

    static int addVisionCircle(
        FrameDataFogObservers &observers,
        FogLosEyeHeights &eyeHeights,
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
        observers.setLosSoftness(slot, kFogLosHardGate);
        eyeHeights[static_cast<std::size_t>(slot)] = kFogVisionLosOff;
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
        FogLosEyeHeights &eyeHeights,
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
            eyeHeights[static_cast<std::size_t>(source)] = losEyeHeight;
            observers.setLosSoftness(source, losSoftness >= 0.0f ? losSoftness : kFogLosHardGate);
        } else {
            observers.losSourceMask_ &= ~(1 << source);
            eyeHeights[static_cast<std::size_t>(source)] = kFogVisionLosOff;
            observers.setLosSoftness(source, kFogLosHardGate);
        }
    }

    void clearAll() {
        if (allUnexplored_)
            return;
        std::fill(cpuBuffer_.begin(), cpuBuffer_.end(), kFogStateUnexplored);
        allUnexplored_ = true;
        dirty_ = true;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_FOG_OF_WAR_H */
