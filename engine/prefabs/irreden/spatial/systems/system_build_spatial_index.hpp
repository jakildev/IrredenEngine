#ifndef SYSTEM_BUILD_SPATIAL_INDEX_H
#define SYSTEM_BUILD_SPATIAL_INDEX_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/spatial/components/component_spatial_index.hpp>

namespace IRSystem {

// Rebuilds the C_SpatialIndex singleton each frame from the world positions
// of every C_WorldTransform entity carrying the C_SpatialQueryable opt-in
// tag. A creation registers this in its UPDATE pipeline AFTER
// PROPAGATE_TRANSFORM (so translation_ is the final world position) and
// BEFORE any system / Lua tick that calls IRPrefab::Spatial::queryRadius.
//
// The system writes the singleton (not an archetype column), so keep it in
// its own pipeline group — it cannot co-execute with anything else that
// touches C_SpatialIndex.
template <> struct System<BUILD_SPATIAL_INDEX> {
    // Resolved once per frame in beginTick (a manager hash lookup), then
    // reused by every per-entity tick — never stored across frames.
    IRComponents::C_SpatialIndex *index_ = nullptr;

    void beginTick() {
        index_ = &IREntity::singleton<IRComponents::C_SpatialIndex>();
        index_->grid_.clear();
    }

    void tick(
        IREntity::EntityId entity,
        const IRComponents::C_WorldTransform &transform,
        const IRComponents::C_SpatialQueryable &
    ) {
        index_->grid_.insert(entity, transform.translation_);
    }

    static SystemId create() {
        return registerSystem<
            BUILD_SPATIAL_INDEX,
            IRComponents::C_WorldTransform,
            IRComponents::C_SpatialQueryable>("BuildSpatialIndex");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_BUILD_SPATIAL_INDEX_H */
