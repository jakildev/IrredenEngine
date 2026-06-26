#ifndef COMPONENT_CANVAS_FOG_OF_WAR_H
#define COMPONENT_CANVAS_FOG_OF_WAR_H

// World-space 2D fog-of-war visibility texture sampled by FOG_TO_TRIXEL
// to mask the rendered scene by per-column visibility state. One cell
// per voxel column on the iso ground plane (X-Y axes, since +Z is the
// downward height axis in this engine's iso convention). Three states:
// `kFogStateUnexplored` = never seen, `kFogStateExplored` = seen but
// not currently visible, `kFogStateVisible` = currently in vision.
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

    /// Float-center, feathered variant of @ref revealRadius for a
    /// smoothly-moving observer. Cells within `radius - feather` of the
    /// (possibly fractional) center @p (cx,cy) are set fully visible; cells
    /// beyond `radius` are untouched; the band between ramps via a Hermite
    /// `smoothstep` over the Euclidean distance so the stored field — and the
    /// rendered edge — evolves continuously with sub-cell motion instead of
    /// popping a whole cell when the center crosses an integer boundary.
    /// `radius` and `feather` are world units; `feather` is clamped to
    /// `[0, radius]`. Combines via **max** so overlapping reveals and the
    /// explored/visible floor compose monotonically (a partial value never
    /// erases an already-brighter cell). Like the integer overload, cells
    /// outside the radius are NOT downgraded — a single moving observer wipes
    /// with `clearAll()` before each call.
    void revealRadius(float cx, float cy, float radius, float feather) {
        if (radius <= 0.0f)
            return;
        const float featherClamped = IRMath::clamp(feather, 0.0f, radius);
        // smoothstep(radius, innerEdge, d) ramps 1.0 -> 0.0 across the feather
        // band (edge0 > edge1 inverts the ramp): 1.0 at d <= innerEdge, 0.0 at
        // d >= radius. innerEdge == radius (feather 0) reproduces a hard disc.
        const float innerEdge = radius - featherClamped;
        const int xMin =
            IRMath::max(static_cast<int>(IRMath::floor(cx - radius)), -kFogOfWarHalfExtent);
        const int xMax =
            IRMath::min(static_cast<int>(IRMath::ceil(cx + radius)), kFogOfWarHalfExtent - 1);
        const int yMin =
            IRMath::max(static_cast<int>(IRMath::floor(cy - radius)), -kFogOfWarHalfExtent);
        const int yMax =
            IRMath::min(static_cast<int>(IRMath::ceil(cy + radius)), kFogOfWarHalfExtent - 1);
        for (int y = yMin; y <= yMax; ++y) {
            for (int x = xMin; x <= xMax; ++x) {
                const float dx = static_cast<float>(x) - cx;
                const float dy = static_cast<float>(y) - cy;
                const float d = IRMath::length(IRMath::vec2(dx, dy));
                const float v = IRMath::smoothstep(radius, innerEdge, d);
                const std::uint8_t value = IRMath::roundFloatToByte(v);
                const std::size_t idx = flatIndex(x, y);
                if (value <= cpuBuffer_[idx])
                    continue;
                cpuBuffer_[idx] = value;
                dirty_ = true;
                allUnexplored_ = false;
            }
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
