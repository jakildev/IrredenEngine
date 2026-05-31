#ifndef IR_SPATIAL_QUERY_H
#define IR_SPATIAL_QUERY_H

// Free-function neighbour-query surface over the C_SpatialIndex singleton.
// Lives in the domain root (like the other prefab-scoped free-function APIs)
// because it is a set of pure functions, not a component or system. Callers
// don't carry the singleton entity id — the namespace owns the lookup.
//
// Contract: BUILD_SPATIAL_INDEX must have run this frame (see
// system_build_spatial_index.hpp). If the index was never built (no creation
// registered the system, or it hasn't ticked yet), the queries return an
// empty result rather than lazy-creating an empty index.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/spatial/components/component_spatial_index.hpp>
#include <irreden/spatial/spatial_grid.hpp>

#include <vector>

namespace IRPrefab::Spatial {

// Nearby entities within `radius` of `center`, as {id, world-position}
// records. `out` is cleared then filled; pass a reused scratch vector to keep
// the query allocation-free. The position is returned inline so the caller
// never needs a per-neighbour foreign component read.
inline void queryRadius(IRMath::vec3 center, float radius, std::vector<SpatialHit> &out) {
    IRComponents::C_SpatialIndex *index =
        IREntity::singletonOrNull<IRComponents::C_SpatialIndex>();
    if (index == nullptr) {
        out.clear();
        return;
    }
    index->grid_.queryRadius(center, radius, out);
}

// Entities inside the inclusive world AABB [min, max]. C++-only in v1 (the
// Lua surface exposes queryRadius only; rect-region Lua queries are a future
// extension).
inline void queryAabb(IRMath::vec3 min, IRMath::vec3 max, std::vector<SpatialHit> &out) {
    IRComponents::C_SpatialIndex *index =
        IREntity::singletonOrNull<IRComponents::C_SpatialIndex>();
    if (index == nullptr) {
        out.clear();
        return;
    }
    index->grid_.queryAabb(min, max, out);
}

} // namespace IRPrefab::Spatial

#endif /* IR_SPATIAL_QUERY_H */
