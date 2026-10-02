#ifndef SYSTEM_GATE_VOXEL_SETS_BY_LOD_H
#define SYSTEM_GATE_VOXEL_SETS_BY_LOD_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/lod_tier_snapshot.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

namespace IRSystem {

// Holds every DENSE voxel set to its LOD band: a set whose resolved tier is
// outside [lodMax_ .. lodMin_] is masked out of its pool and skipped by the
// per-frame voxel update arms, exactly like a hidden set. Register in UPDATE
// after LOD_UPDATE and before UPDATE_VOXEL_SET_CHILDREN, so a set that comes
// back into band is positioned and rasterized the same frame. A creation that
// never registers it leaves every set drawn regardless of band.
template <> struct System<GATE_VOXEL_SETS_BY_LOD> {
    IRPrefab::Lod::TierSnapshot lod_;

    void beginTick() {
        lod_.capture();
    }

    void tick(IREntity::EntityId entity, IRComponents::C_VoxelSetNew &voxelSet) {
        voxelSet.setLodCulled(
            IRRender::shouldSkipAtLod(voxelSet.lodMin_, voxelSet.lodMax_, lod_.resolve(entity))
        );
    }

    static SystemId create() {
        return registerSystem<GATE_VOXEL_SETS_BY_LOD, IRComponents::C_VoxelSetNew>(
            "GateVoxelSetsByLod"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GATE_VOXEL_SETS_BY_LOD_H */
