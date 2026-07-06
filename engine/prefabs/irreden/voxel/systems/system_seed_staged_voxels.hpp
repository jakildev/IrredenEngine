#ifndef SYSTEM_SEED_STAGED_VOXELS_H
#define SYSTEM_SEED_STAGED_VOXELS_H

// SEED_STAGED_VOXELS (UPDATE pipeline) — the W-10 canvas-attach / post-load
// seed pass (#2217, epic #667). After IRWorld::loadWorld restores the ECS
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

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/voxel/components/component_voxel_set.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<SEED_STAGED_VOXELS> {
    static SystemId create() {
        return createSystem<C_VoxelSetNew>("SeedStagedVoxels", [](C_VoxelSetNew &voxelSet) {
            voxelSet.attachToCanvas();
        });
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SEED_STAGED_VOXELS_H */
