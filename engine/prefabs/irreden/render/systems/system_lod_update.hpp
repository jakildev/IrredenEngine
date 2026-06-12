#ifndef SYSTEM_LOD_UPDATE_H
#define SYSTEM_LOD_UPDATE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_active_lod_level.hpp>
#include <irreden/render/lod_utils.hpp>

namespace IRSystem {

// Register before PROPAGATE_TRANSFORM in UPDATE so the singleton is current before RENDER ticks.
// Takes max(zoom.x, zoom.y) — render_manager.cpp snaps to uniform power-of-two so x==y in practice.
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
