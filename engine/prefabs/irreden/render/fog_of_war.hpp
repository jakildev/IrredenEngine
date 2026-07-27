#ifndef IR_PREFAB_FOG_OF_WAR_H
#define IR_PREFAB_FOG_OF_WAR_H

// Driver-side API for the render pipeline's FOG_TO_TRIXEL pass. All
// operations apply to the active canvas's `C_CanvasFogOfWar` component
// and silently no-op when no canvas is active or the active canvas
// does not own one — this lets scripts and init code run before the
// canvas is fully wired without crashing.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

#include <cstdint>

namespace IRPrefab::Fog {

namespace detail {

inline IRComponents::C_CanvasFogOfWar *activeFogComponent() {
    const IREntity::EntityId canvas = IRRender::getActiveCanvasEntity();
    if (canvas == IREntity::kNullEntity)
        return nullptr;
    auto opt = IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(canvas);
    if (!opt.has_value())
        return nullptr;
    return *opt;
}

} // namespace detail

/// Set a single fog cell at world-space voxel column @p (worldX, worldY).
/// State values: 0 = unexplored, 128 = explored, 255 = visible.
/// Out-of-range writes are silently dropped.
inline void setCell(int worldX, int worldY, std::uint8_t state) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->setCell(worldX, worldY, state);
    }
}

/// Read the fog state at @p (worldX, worldY). Returns
/// `kFogStateUnexplored` if the active canvas has no fog component or
/// the coordinate is out of range.
inline std::uint8_t getCell(int worldX, int worldY) {
    if (auto *fog = detail::activeFogComponent()) {
        return fog->getCell(worldX, worldY);
    }
    return IRComponents::kFogStateUnexplored;
}

/// Mark every cell within @p radius (Euclidean distance, since #1994) of
/// @p (cx, cy) as visible. See `C_CanvasFogOfWar::revealRadius` for the v1
/// contract around the cells that are NOT downgraded.
inline void revealRadius(int cx, int cy, int radius) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->revealRadius(cx, cy, radius);
    }
}

/// Replace the live vision set with a single analytic disc centered at the
/// (fractional) world column @p (cx, cy), @p radius world units. This is the
/// SMOOTH, render-resolution reveal: evaluated per pixel in the fog shader, so
/// the edge is crisp at game resolution, tracks sub-voxel observer motion
/// without grid quantization, and reveals partial voxels at the boundary —
/// distinct from the voxel-grid `revealRadius`. @p edge is the edge softness
/// in world units (default reads as antialiasing). @p observerZ + @p zCost
/// (#2260) add a height penalty — the effective reveal distance becomes
/// `dist_xy + zCost * |z - observerZ|`, so matter far above/below the
/// observer's height reveals less at the same XY; @p zCost 0 (the default) is
/// the plain 2D disc. For multiple sources, call `clearVisionCircles` then
/// `addVisionCircle` per source. Combines (with the grid and other circles) via
/// max.
inline void setVisionCircle(
    float cx,
    float cy,
    float radius,
    float edge = IRComponents::kFogVisionEdgeDefault,
    float observerZ = 0.0f,
    float zCost = 0.0f
) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->clearVisionCircles();
        fog->addVisionCircle(cx, cy, radius, edge, observerZ, zCost);
    }
}

/// Append one analytic vision disc to the live set (up to
/// `kMaxFogVisionCircles`). See `setVisionCircle` for disc semantics (including
/// the @p observerZ / @p zCost height penalty, #2260); use this after
/// `clearVisionCircles` to drive several vision sources in one frame.
inline void addVisionCircle(
    float cx,
    float cy,
    float radius,
    float edge = IRComponents::kFogVisionEdgeDefault,
    float observerZ = 0.0f,
    float zCost = 0.0f
) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->addVisionCircle(cx, cy, radius, edge, observerZ, zCost);
    }
}

/// Drop every live analytic vision disc → grid-only fog.
inline void clearVisionCircles() {
    if (auto *fog = detail::activeFogComponent()) {
        fog->clearVisionCircles();
    }
}

/// Reset every cell to `kFogStateUnexplored`.
inline void clear() {
    if (auto *fog = detail::activeFogComponent()) {
        fog->clearAll();
    }
}

/// Attach both components FOG_TO_TRIXEL's archetype requires to @p canvas:
/// C_TrixelCanvasRenderBehavior (added only if absent, preserving any prior
/// customized behavior component) and a fresh C_CanvasFogOfWar. FOG_TO_TRIXEL
/// silently no-ops on a canvas missing either, so co-attaching here removes
/// that footgun from call sites. @p revealRadius > 0 also reveals an
/// origin-centered disc of that radius on @p canvas (pass kFogOfWarSize for a
/// full reveal); 0 (default) attaches only, leaving the grid unexplored.
inline void attachToCanvas(IREntity::EntityId canvas, int revealRadius = 0) {
    if (!IREntity::getComponentOptional<IRComponents::C_TrixelCanvasRenderBehavior>(canvas)
             .has_value())
        IREntity::setComponent(canvas, IRComponents::C_TrixelCanvasRenderBehavior{});
    IREntity::setComponent(canvas, IRComponents::C_CanvasFogOfWar{});
    if (revealRadius > 0) {
        if (auto opt = IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(canvas))
            (*opt)->revealRadius(0, 0, revealRadius);
    }
}

} // namespace IRPrefab::Fog

#endif /* IR_PREFAB_FOG_OF_WAR_H */
