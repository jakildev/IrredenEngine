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

#include <cstdint>

namespace IRPrefab::Fog {

namespace detail {

inline IRComponents::C_CanvasFogOfWar *activeFogComponent() {
    const IREntity::EntityId canvas = IRRender::getActiveCanvasEntity();
    if (canvas == IREntity::kNullEntity) return nullptr;
    auto opt = IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(canvas);
    if (!opt.has_value()) return nullptr;
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

/// Mark every cell within @p radius (taxicab distance) of @p (cx, cy)
/// as visible. See `C_CanvasFogOfWar::revealRadius` for the v1 contract
/// around the cells that are NOT downgraded.
inline void revealRadius(int cx, int cy, int radius) {
    if (auto *fog = detail::activeFogComponent()) {
        fog->revealRadius(cx, cy, radius);
    }
}

inline void clear() {
    if (auto *fog = detail::activeFogComponent()) {
        fog->clearAll();
    }
}

} // namespace IRPrefab::Fog

#endif /* IR_PREFAB_FOG_OF_WAR_H */
