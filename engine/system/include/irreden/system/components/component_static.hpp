#ifndef COMPONENT_STATIC_H
#define COMPONENT_STATIC_H

#include <irreden/entity/ir_entity_types.hpp>

#include <cstdint>
#include <type_traits>

// The two tags that make an entity a static resident
// (docs/design/static-residents.md). They live in the system layer because
// the scheduler applies the matching rule and has to name them.
//
//   no tag                 dynamic   every system matches as before
//   C_Static               settling  pose writers no longer match
//   C_Static + C_Settled   settled   deriving systems no longer match

namespace IRComponents {

/// The owner's declaration that an entity's pose inputs are fixed.
struct C_Static {
    /// Number of the first round that must open after the entity's inputs
    /// last changed; zero until the transition step first sees the entity.
    /// Engine-owned: only `IRPrefab::Static::drain` writes it.
    std::uint64_t settleStamp_ = 0;
};

/// Engine-owned: the deriving systems have run over the entity's frozen
/// inputs. Never exists without `C_Static`, and never survives an archetype
/// move that does not name it.
struct C_Settled {};

static_assert(std::is_same_v<decltype(C_Static::settleStamp_), std::uint64_t>);

} // namespace IRComponents

namespace IREntity {

template <> inline constexpr bool kClearedByForeignMove<IRComponents::C_Settled> = true;
// A static entity's world transform depends on its parent chain.
template <> inline constexpr bool kTracksInputChanges<IRComponents::C_Static> = true;

} // namespace IREntity

#endif /* COMPONENT_STATIC_H */
