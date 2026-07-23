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
// v1 scope: the texture and a CPU-side mirror plus a dirty flag the
// system uses to gate the per-frame `subImage2D` upload. The upload is
// performed by `VOXEL_TO_TRIXEL_STAGE_1` (#2008) — which also reads the
// fog to cull unexplored-column voxels and so needs it current — not by
// `FOG_TO_TRIXEL`, which is now a read-only consumer of the
// already-uploaded texture. This is the documented exception to the
// "no dirty flags on components" rule —
// see `.claude/rules/cpp-ecs.md` § "No dirty flags on components".
// The exception applies because the texture is CPU-authored,
// GPU-read-only, and a per-cell `subImage2D` would split
// `revealRadius`'s loop into hundreds of API calls. T-161 evaluated
// migrating to per-region `subImage2D` and deferred — see
// `docs/design/fog-of-war-upload-strategy.md` for the analysis, the
// trigger conditions for revisiting, and the mechanical migration
// sketch. Population is driver-side: gameplay calls
// `IRPrefab::Fog::setCell` / `IRPrefab::Fog::revealRadius` (see
// `render/fog_of_war.hpp`) to drive the visibility set directly. LOS
// ray casting against the occupancy grid (`castLOS`), heightmap-aware
// LOS, and the visible→explored fade callback (`fadeExplored`) are
// deferred to follow-up tasks — the fog-of-war foundation here lets
// the render pass and Lua-side scripts ship before those algorithms
// land.
//
// Sized to match the light-occlusion SSBO's 256×256 footprint on the ground
// plane (256 KiB CPU+GPU) and using the same `[-halfExtent, +halfExtent)`
// world-centered cell convention. One cell per integer voxel column.
// Out-of-range writes are silently dropped; out-of-range reads return
// `kFogStateUnexplored`. Out-of-range pixels in the shader are treated
// as visible via an explicit bounds check (image bindings bypass sampler
// wrap modes).
//
// `kFogOfWarSize` / `kFogOfWarHalfExtent` are mirrored as literals in
// `c_fog_to_trixel.glsl` and `metal/c_fog_to_trixel.metal`. Renaming
// the C++ constants requires editing both shader files in lockstep.

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/texture.hpp>

#include <cstdint>
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
// `kMaxFogVisionCircles` is mirrored as a literal in `c_fog_to_trixel.glsl` /
// `metal/c_fog_to_trixel.metal` (the UBO array length); changing it requires
// editing both shaders and re-checking the std140 / Metal struct size below.
constexpr int kMaxFogVisionCircles = 8;
constexpr float kFogVisionEdgeDefault = 0.0f;

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
    /// Reserved padding lane. Previously the system-stamped
    /// light-occlusion-grid availability flag for the retired ray+occupancy
    /// cut variant; `c_fog_to_trixel`'s geometric cross-section cap needs no
    /// occupancy source, so the lane is unread (kept for the layout).
    std::int32_t pad0_ = 0;
    std::int32_t pad1_ = 0;
    std::int32_t pad2_ = 0;
    /// Per-circle height penalty (#2260), std140-appended AFTER the tail so
    /// every EXISTING member offset is unchanged — a shader that reads only
    /// `visionCircles_` / `visionCircleCount_` (c_voxel_to_trixel_stage_2,
    /// c_voxel_visibility_compact) sees byte-identical bytes and needs no edit.
    /// `visionCircleHeights_[i]` = (observerZ, zCost, 0, 0): the fog reveal adds
    /// `zCost * |z - observerZ|` to the radial XY distance, so matter far
    /// above/below the observer's height reveals less at the same XY (iso +Z is
    /// the downward height axis). zCost 0 (the default for every existing
    /// caller) makes the penalty term exactly 0, so the whole struct
    /// uploads/decodes byte-identically to the pre-#2260 layout. Only the first
    /// `visionCircleCount_` entries are read, paired 1:1 with `visionCircles_`.
    IRMath::vec4 visionCircleHeights_[kMaxFogVisionCircles] = {};
};
static_assert(
    sizeof(FrameDataFogObservers) == 2 * kMaxFogVisionCircles * 16 + 16,
    "FrameDataFogObservers must stay std140/Metal-tight (vec4[N] + ivec4 tail + vec4[N])"
);

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
    /// / `addVisionCircle`; empty (count 0) means grid-only (legacy behavior).
    FrameDataFogObservers observers_{};

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
          ) {}

    void onDestroy() {
        IRRender::destroyResource<Texture2D>(texture_.first);
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

    /// Drop all live vision circles → grid-only fog (legacy behavior). A
    /// single moving observer calls this then `addVisionCircle` each frame.
    void clearVisionCircles() {
        observers_.visionCircleCount_ = 0;
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
    /// @p observerZ + @p zCost (#2260) shape the disc into an XY radius with a
    /// height penalty: the effective reveal distance is
    /// `dist_xy + zCost * |z - observerZ|`, so matter at the observer's height
    /// reveals to the full radius while a tall pillar top / deep pit floor at
    /// the same XY reveals less (iso +Z is the downward height axis). @p zCost 0
    /// (the default) is the back-compat plain 2D disc — byte-identical to the
    /// pre-#2260 reveal.
    void addVisionCircle(
        float cx,
        float cy,
        float radius,
        float edge = kFogVisionEdgeDefault,
        float observerZ = 0.0f,
        float zCost = 0.0f
    ) {
        if (radius <= 0.0f || observers_.visionCircleCount_ >= kMaxFogVisionCircles)
            return;
        observers_.visionCircles_[observers_.visionCircleCount_] =
            IRMath::vec4(cx, cy, radius, IRMath::max(edge, 0.0f));
        // The zCost clamp is load-bearing, not defensive hygiene: it is what
        // keeps c_voxel_visibility_compact's z-FREE coarse cull a superset of
        // stage 1's z-AWARE own-column drop. That holds only because
        // `distEff = dist + zCost * |z - observerZ| >= dist` for zCost >= 0, so
        // the penalized reveal is pointwise <= the z-free one and the drop can
        // only ever drop MORE. A negative zCost inverts it, and the compact pass
        // culls voxels stage 1 would still render — matter silently missing,
        // with no shader error. `observers_` is public, so a caller that writes
        // visionCircleHeights_ directly owns this invariant itself.
        observers_.visionCircleHeights_[observers_.visionCircleCount_] =
            IRMath::vec4(observerZ, IRMath::max(zCost, 0.0f), 0.0f, 0.0f);
        ++observers_.visionCircleCount_;
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
