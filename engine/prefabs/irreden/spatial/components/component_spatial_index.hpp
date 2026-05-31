#ifndef COMPONENT_SPATIAL_INDEX_H
#define COMPONENT_SPATIAL_INDEX_H

#include <irreden/spatial/spatial_grid.hpp>

namespace IRComponents {

// Singleton row owning the world-space neighbour index. Rebuilt every frame
// by the BUILD_SPATIAL_INDEX system (UPDATE pipeline) from C_WorldTransform +
// C_SpatialQueryable entities; read by IRPrefab::Spatial::queryRadius /
// queryAabb. Reach it via IREntity::singleton<C_SpatialIndex>() (lazy-created
// on first access). The grid's bucket capacity is reused across frames — see
// spatial_grid.hpp for the allocation contract.
struct C_SpatialIndex {
    IRPrefab::Spatial::SpatialGrid grid_;
};

// Opt-in tag: only entities carrying this AND C_WorldTransform are inserted
// into the spatial index. Tagging is explicit so a world canvas's static
// voxels (and any other transform-bearing entity that nobody queries for)
// pay nothing.
struct C_SpatialQueryable {};

} // namespace IRComponents

#endif /* COMPONENT_SPATIAL_INDEX_H */
