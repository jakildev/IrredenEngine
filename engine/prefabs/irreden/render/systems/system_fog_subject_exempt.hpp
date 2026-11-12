#ifndef SYSTEM_FOG_SUBJECT_EXEMPT_H
#define SYSTEM_FOG_SUBJECT_EXEMPT_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/fog_settled_gate.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

namespace IRSystem {

// Realizes a construction-time or script-attached `C_FogExempt` marker on a
// voxel set: the carrier is pinned at factor 255 so every raster route renders
// the set whole, with no field lookup. `setSubjectClass(e, EXEMPT)` stamps the
// same carrier synchronously; this pass covers the marker routes. Sits ahead
// of FOG_SUBJECT_ADOPT and visits the same population: sets on the fogged
// active canvas.
template <> struct System<FOG_SUBJECT_EXEMPT> {
    static constexpr std::uint32_t kExemptCarrier =
        IRComponents::VoxelReserved::kFogBody |
        (0xFFu << IRComponents::VoxelReserved::kFogBodyFactorShift);

    IREntity::EntityId activeCanvas_ = IREntity::kNullEntity;
    // The fogged active canvas's pool; null leaves the pass inert.
    IRComponents::C_VoxelPool *activePool_ = nullptr;
    IRPrefab::Fog::SettledGate gate_;

    void beginTick() {
        activeCanvas_ = IRRender::getActiveCanvasEntityOrNull();
        activePool_ = nullptr;
        if (activeCanvas_ != IREntity::kNullEntity &&
            IREntity::getComponentOptional<IRComponents::C_CanvasFogOfWar>(activeCanvas_)
                .has_value()) {
            if (auto pool =
                    IREntity::getComponentOptional<IRComponents::C_VoxelPool>(activeCanvas_)) {
                activePool_ = *pool;
            }
        }
        // The carrier pin reads the canvas and the pool, which is resolved
        // only while fog is attached; the field's contents never change it.
        gate_.begin(activeCanvas_, activePool_, nullptr, {});
    }

    void tick(IRComponents::C_VoxelSetNew &voxelSet, const IRComponents::C_FogExempt &) {
        if (activePool_ == nullptr) {
            return;
        }
        if (!IRPrefab::Fog::isOnFogCanvas(voxelSet, activeCanvas_)) {
            return;
        }
        if (IRPrefab::Fog::bodyCarrierBits(*activePool_, voxelSet) == kExemptCarrier) {
            return;
        }
        IRPrefab::Fog::stampBodyCarrier(*activePool_, voxelSet, true, 255);
    }

    void tick(
        const IREntity::Archetype &type,
        std::vector<IREntity::EntityId> &entities,
        std::vector<IRComponents::C_VoxelSetNew> &voxelSets,
        std::vector<IRComponents::C_FogExempt> &exempt
    ) {
        gate_.walkNode(*this, 0, type, entities, voxelSets, exempt);
    }

    static SystemId create() {
        return registerSystem<
            FOG_SUBJECT_EXEMPT,
            IRComponents::C_VoxelSetNew,
            IRComponents::C_FogExempt,
            Exclude<IRComponents::C_FogRevealed>,
            GatesSettled,
            MainThread>("FogSubjectExempt");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_FOG_SUBJECT_EXEMPT_H */
