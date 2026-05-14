#ifndef SYSTEM_LOD_UPDATE_H
#define SYSTEM_LOD_UPDATE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_active_lod_level.hpp>
#include <irreden/render/lod_utils.hpp>

namespace IRSystem {

// UPDATE-pipeline driver for level-of-detail tier selection. Each frame
// snapshots the camera zoom, maps it through computeLodLevel(), and
// writes the result into the C_ActiveLodLevel singleton. SHAPES_TO_TRIXEL
// reads the singleton at beginTick to filter shapes by lodMin_.
//
// Register before any RENDER-pipeline system that consumes the tier
// (today: SHAPES_TO_TRIXEL). The work lives in beginTick so the
// singleton row's value is correct on every frame regardless of
// archetype-iteration order — IREntity::singleton<> lazy-creates the
// row if it is absent and returns a live reference into it.
//
// `getCameraZoom()` returns a vec2 because the engine accepts non-uniform
// zoom in principle; render_manager.cpp snaps incoming requests to a
// uniform power-of-two so x and y match in practice. Taking the max
// matches the convention used by the subdivision-count snap in
// `render_manager.cpp` and degrades gracefully if a future creation
// authors non-uniform zoom.
template <> struct System<LOD_UPDATE> {
    void beginTick() {
        const IRMath::vec2 zoom = IRRender::getCameraZoom();
        const float zoomScalar = IRMath::max(zoom.x, zoom.y);
        IREntity::singleton<IRComponents::C_ActiveLodLevel>().current_ =
            IRRender::computeLodLevel(zoomScalar);
    }

    // Singleton-only work happens in beginTick; the per-entity tick is
    // unused but is required by registerSystem<>'s contract.
    void tick(IRComponents::C_ActiveLodLevel &) {}

    static SystemId create() {
        return registerSystem<LOD_UPDATE, IRComponents::C_ActiveLodLevel>("LodUpdate");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_LOD_UPDATE_H */
