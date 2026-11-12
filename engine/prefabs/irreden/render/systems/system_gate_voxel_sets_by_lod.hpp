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
//
// Gated for settled static residents (docs/design/static-residents.md): the
// verdict reads the world-scoped active tier, so the system keeps matching
// settled rows and walks them only on a dispatch where that tier changed. A
// band change through `setLodBand`, or attaching or detaching
// `C_LodTierOverride`, returns the entity to settling, where it is walked on
// every dispatch.
template <> struct System<GATE_VOXEL_SETS_BY_LOD> {
    IRPrefab::Lod::TierSnapshot lod_;
    // The active tier as of the last dispatch: a world-scoped input with no
    // mutator to push from.
    IRRender::LodLevel seenActive_ = IRRender::LodLevel::LOD_4;
    bool primed_ = false;
    bool armed_ = false;
    IREntity::ComponentId settledTag_ = IREntity::kNullComponent;

    void beginTick() {
        lod_.capture();
        armed_ = !primed_ || lod_.active_ != seenActive_;
        primed_ = true;
        seenActive_ = lod_.active_;
        settledTag_ = IREntity::getEntityManager().findComponentType<IRComponents::C_Settled>();
    }

    void tick(
        const IREntity::Archetype &type,
        std::vector<IREntity::EntityId> &entities,
        std::vector<IRComponents::C_VoxelSetNew> &voxelSets
    ) {
        if (!armed_ && settledTag_ != IREntity::kNullComponent && type.contains(settledTag_)) {
            reportRowsWalked(0);
            return;
        }
        for (std::size_t i = 0; i < entities.size(); ++i) {
            tick(entities[i], voxelSets[i]);
        }
        reportRowsWalked(entities.size());
    }

    void tick(IREntity::EntityId entity, IRComponents::C_VoxelSetNew &voxelSet) {
        voxelSet.setLodCulled(
            IRRender::shouldSkipAtLod(voxelSet.lodMin_, voxelSet.lodMax_, lod_.resolve(entity))
        );
    }

    static SystemId create() {
        return registerSystem<
            GATE_VOXEL_SETS_BY_LOD,
            IRComponents::C_VoxelSetNew,
            GatesSettled,
            MainThread>("GateVoxelSetsByLod");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GATE_VOXEL_SETS_BY_LOD_H */
