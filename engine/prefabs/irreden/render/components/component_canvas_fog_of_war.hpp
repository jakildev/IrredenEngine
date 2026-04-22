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
// system uses to gate the per-frame `subImage2D` upload. Population is
// driver-side: gameplay calls `IRRender::setFogCell` /
// `IRRender::revealRadius` to drive the visibility set directly. LOS
// ray casting against the occupancy grid (`castLOS`), heightmap-aware
// LOS, and the visible→explored fade callback (`fadeExplored`) are
// deferred to follow-up tasks — the fog-of-war foundation here lets
// the render pass and Lua-side scripts ship before those algorithms
// land.
//
// Sized to match `C_OccupancyGrid`'s 256×256 footprint on the ground
// plane (256 KiB CPU+GPU) and using the same `[-halfExtent, +halfExtent)`
// world-centered cell convention — see `component_occupancy_grid.hpp`
// for the precedent. One cell per integer voxel column. Out-of-range
// writes are silently dropped; out-of-range reads return
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
    /// Set by any cell-mutating helper; cleared by the system after the
    /// `subImage2D` upload completes. Also set on construction so the
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
              static_cast<std::size_t>(kFogOfWarSize) *
                  static_cast<std::size_t>(kFogOfWarSize),
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
        if (!inBounds(wx, wy)) return kFogStateUnexplored;
        return cpuBuffer_[flatIndex(wx, wy)];
    }

    void setCell(int wx, int wy, std::uint8_t state) {
        if (!inBounds(wx, wy)) return;
        const std::size_t idx = flatIndex(wx, wy);
        if (cpuBuffer_[idx] == state) return;
        cpuBuffer_[idx] = state;
        dirty_ = true;
        if (state != kFogStateUnexplored) allUnexplored_ = false;
    }

    /// Mark every cell within `radius` (taxicab distance) of `(cx,cy)`
    /// as visible. Cells previously visible but now outside the radius
    /// are NOT downgraded — that lifecycle belongs to the deferred
    /// `fadeExplored` pass, since downgrade requires knowing every
    /// vision source's union (game-state-specific). v1 callers that
    /// want a single moving observer can wipe the texture themselves
    /// before each `revealRadius` call.
    void revealRadius(int cx, int cy, int radius) {
        if (radius < 0) return;
        const int xMin = std::max(cx - radius, -kFogOfWarHalfExtent);
        const int xMax = std::min(cx + radius, kFogOfWarHalfExtent - 1);
        const int yMin = std::max(cy - radius, -kFogOfWarHalfExtent);
        const int yMax = std::min(cy + radius, kFogOfWarHalfExtent - 1);
        for (int y = yMin; y <= yMax; ++y) {
            for (int x = xMin; x <= xMax; ++x) {
                const int dx = x - cx;
                const int dy = y - cy;
                if (std::abs(dx) + std::abs(dy) > radius) continue;
                const std::size_t idx = flatIndex(x, y);
                if (cpuBuffer_[idx] == kFogStateVisible) continue;
                cpuBuffer_[idx] = kFogStateVisible;
                dirty_ = true;
                allUnexplored_ = false;
            }
        }
    }

    void clearAll() {
        if (allUnexplored_) return;
        std::fill(cpuBuffer_.begin(), cpuBuffer_.end(), kFogStateUnexplored);
        allUnexplored_ = true;
        dirty_ = true;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_FOG_OF_WAR_H */
