#ifndef SYSTEM_LOD_UPDATE_H
#define SYSTEM_LOD_UPDATE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_active_lod_level.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/render/lod_zoom_policy.hpp>
#include <irreden/render/lod_zoom_profile.hpp>

namespace IRSystem {

// Register before PROPAGATE_TRANSFORM in UPDATE so the singleton is current before RENDER ticks.
// Takes max(zoom.x, zoom.y) — the main camera's zoom is uniform, so x == y in practice.
// The tier moves from the previous one under the main camera's zoom profile
// (IRPrefab::Lod::mainZoomProfile), so a hysteresis deadband holds it.
template <> struct System<LOD_UPDATE> {
    void beginTick() {
        writeActiveTier(IRRender::getCameraZoom());
    }

    // The zoom -> singleton write on its own, so a caller with no RenderManager
    // (a headless test) can drive the tier from a zoom value.
    static void writeActiveTier(IRMath::vec2 zoom) {
        writeActiveTier(zoom, IRPrefab::Lod::mainZoomProfile());
    }

    static void writeActiveTier(IRMath::vec2 zoom, const IRRender::LodZoomProfile &profile) {
        auto &active = IREntity::singleton<IRComponents::C_ActiveLodLevel>();
        active.current_ = active.history_.advance(profile, IRMath::max(zoom.x, zoom.y));
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
