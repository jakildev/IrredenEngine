#ifndef COMPONENT_COLLISION_PAIR_BUFFER_H
#define COMPONENT_COLLISION_PAIR_BUFFER_H

#include <cstdint>
#include <vector>

#include <irreden/entity/ir_entity_types.hpp>

namespace IRComponents {

// One confirmed overlap between two collider entities for the current
// frame. Produced by COLLISION_NOTE_PLATFORM (which has both colliders and
// both C_CollisionLayers in hand during its scan, so it stamps each
// entity's layer here) and consumed by COLLISION_DISPATCH_LUA_OVERLAP. The
// batched-vector contact pattern from `.claude/rules/cpp-ecs.md`
// §"Foreign-entity lookups": the layers travel with the pair so the
// consumer never calls getComponent on the foreign entity.
//
// Canonical ordering: `a_ < b_`, with `layerA_` the layer of `a_` and
// `layerB_` the layer of `b_`. The producer emits each unordered pair
// exactly once (when the smaller-id entity is the scan's "self").
struct ContactPair {
    IREntity::EntityId a_;
    IREntity::EntityId b_;
    std::uint32_t layerA_;
    std::uint32_t layerB_;
};

// Singleton component holding the frame's confirmed overlap pairs. Lives on
// the entity returned by `IREntity::singleton<C_CollisionPairBuffer>()`
// (lazily created on first access — same pattern as C_SimClock and the
// modifier-resolver globals). COLLISION_NOTE_PLATFORM clears it in its
// beginTick and fills it during the scan; COLLISION_DISPATCH_LUA_OVERLAP
// reads it. The capacity is retained across frames (clear-not-release), so
// the steady state allocates nothing.
struct C_CollisionPairBuffer {
    std::vector<ContactPair> pairs_;

    C_CollisionPairBuffer() = default;
};

} // namespace IRComponents

#endif /* COMPONENT_COLLISION_PAIR_BUFFER_H */
