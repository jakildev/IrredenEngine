#ifndef COMPONENT_OVERLAP_CONTACT_BATCH_H
#define COMPONENT_OVERLAP_CONTACT_BATCH_H

#include <irreden/ir_math.hpp>
#include <irreden/entity/ir_entity_types.hpp>

#include <cstdint>
#include <vector>

using IRMath::vec3;

namespace IRComponents {

// One confirmed AABB overlap, stamped by COLLISION_NOTE_PLATFORM with BOTH
// colliders' collision layers in hand (#1817). Because the producer already
// holds both `C_CollisionLayer`s during its broad+narrow scan, it records
// each entity's layer here — so the SYSTEM_DISPATCH_LUA_OVERLAP consumer never
// reaches back to the foreign entity with `getComponent` (the batched-vector
// contact pattern, `.claude/rules/cpp-ecs.md` §"Foreign-entity lookups").
//
// `a_ < b_` is canonical: the producer emits each overlapping pair exactly
// once (gated on `self < other`), so the consumer never sees a mirror
// duplicate. `layerA_` is `a_`'s layer, `layerB_` is `b_`'s — the two stay
// index-aligned with the entity ids so the dispatcher can map a layer-pair
// handler's registered arg order back to the right entity.
struct ContactPair {
    IREntity::EntityId a_;
    IREntity::EntityId b_;
    std::uint32_t layerA_;
    std::uint32_t layerB_;
    vec3 normal_;
};

// Singleton component holding this frame's confirmed overlap pairs.
// Producer (COLLISION_NOTE_PLATFORM) appends; consumer
// (SYSTEM_DISPATCH_LUA_OVERLAP) reads then clears. Reached anywhere via
// `IREntity::singleton<C_OverlapContactBatch>()` / `singletonOrNull<...>()`,
// which is what lets the producer and the dispatch system share the vector
// without a cross-system SystemId handoff.
//
// Its existence is the opt-in switch: the dispatch system creates the
// singleton in `create()`, so a creation that does NOT register the
// dispatcher leaves it absent and the producer skips pair emission entirely
// (`singletonOrNull` returns nullptr) — existing COLLISION_NOTE_PLATFORM users
// (the music demo) pay nothing.
struct C_OverlapContactBatch {
    std::vector<ContactPair> pairs_;
};

} // namespace IRComponents

#endif /* COMPONENT_OVERLAP_CONTACT_BATCH_H */
