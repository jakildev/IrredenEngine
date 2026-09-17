#ifndef SYSTEM_SEED_STAGED_VOXELS_H
#define SYSTEM_SEED_STAGED_VOXELS_H

// SEED_STAGED_VOXELS (UPDATE pipeline) — the canvas-attach / post-load
// seed pass. After IRWorld::loadWorld restores the ECS
// graph, every C_VoxelSetNew deserialized by SaveSerialize<C_VoxelSetNew>
// arrives in STAGED mode (numVoxels_ == 0, pendingVoxels_ holds the canonical
// voxel data, empty pool spans) — invisible to the pool pipeline, which gates
// on numVoxels_ > 0. This system moves each staged set into a live pool span
// via C_VoxelSetNew::attachToCanvas so it renders; everything downstream
// (lighting / AO / sun-shadow / fog textures) re-derives from the re-seeded
// pool on the next render tick with no explicit work.
//
// No dirty flag: attachToCanvas is a no-op once a set is pool-resident, so the
// "is this set staged" gate is the set's own honest state (pendingVoxels_
// non-empty), never a bool the caller must set/clear (see cpp-ecs.md "No dirty
// flags"). The system iterates C_VoxelSetNew every tick and seeds only the
// still-staged ones; a fully-seeded scene leaves it a cheap O(sets) walk of
// two-field early-returns. Register it in the UPDATE pipeline BEFORE
// UPDATE_VOXEL_SET_CHILDREN so a freshly-seeded set's positions upload the same
// frame it attaches. A set that stages before any render context exists (a
// headless-authored set in a canvas-active world) is picked up here on the
// first tick a pool is resolvable.
//
// A rigged set's per-voxel bone→slot stamps live in the pool, not the set, so
// they do not survive a re-stage (canvas teardown) or a load. The seed lands
// the set on rigid follow and endTick re-stamps every set seeded this tick
// through `IRPrefab::JointTransform::seedVoxelBoneSlots` — a no-op for a set
// with no skeleton block. Deferred to endTick so the per-entity lookup it
// costs stays off the per-entity tick.
//
// The tick takes the archetype-batch form so the seeded list is sized once
// per batch, to the row count, before the row loop — the loop itself never
// grows it (cpp-ecs.md "Allocations in hot tick paths"). Capacity persists
// across ticks, so a steady-state scene pays no allocation at all.

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/systems/system_update_joint_matrices.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstddef>
#include <vector>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<SEED_STAGED_VOXELS> {
    // Entities whose set became pool-resident this tick. Cleared per tick;
    // capacity persists.
    std::vector<IREntity::EntityId> seeded_;

    void beginTick() {
        seeded_.clear();
    }

    void tick(
        const Archetype &,
        std::vector<IREntity::EntityId> &entities,
        std::vector<C_VoxelSetNew> &voxelSets
    ) {
        seeded_.reserve(seeded_.size() + entities.size());
        for (std::size_t i = 0; i < entities.size(); ++i) {
            if (voxelSets[i].attachToCanvas()) {
                seeded_.push_back(entities[i]);
            }
        }
    }

    void endTick() {
        for (const IREntity::EntityId entity : seeded_) {
            IRPrefab::JointTransform::seedVoxelBoneSlots(entity);
        }
    }

    static SystemId create() {
        return registerSystem<SEED_STAGED_VOXELS, C_VoxelSetNew>("SeedStagedVoxels");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SEED_STAGED_VOXELS_H */
