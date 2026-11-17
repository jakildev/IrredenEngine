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

namespace IREntity {
template <typename Component> class IComponentDataImpl;
} // namespace IREntity

namespace IRSystem::detail {
class SettlePromoter;
} // namespace IRSystem::detail

namespace IRComponents {

/// The owner's declaration that an entity's pose inputs are fixed.
///
/// The settle stamp travels only with its own row. A copy, constructed or
/// assigned, arrives with a zero stamp, so a `C_Static` copied from another
/// entity, or an old copy of this entity's own written back, is stamped as a
/// new row; a zero stamp only ever adds a round. Moves keep it: column growth
/// and archetype moves move rows. Move assignment is private, so no public
/// code can put one row's stamp on another row.
struct C_Static {
    C_Static() = default;
    C_Static(const C_Static &) noexcept {}
    C_Static &operator=(const C_Static &) noexcept {
        settleStamp_ = 0;
        return *this;
    }
    C_Static(C_Static &&) noexcept = default;
    ~C_Static() = default;

    /// Number of the first round that must open after the entity's inputs
    /// last changed; zero until the transition step first sees the entity.
    std::uint64_t settleStamp() const {
        return settleStamp_;
    }

  private:
    // The swap-and-pop that packs a column after a row leaves it.
    C_Static &operator=(C_Static &&) noexcept = default;

    friend class IRSystem::detail::SettlePromoter;
    friend class IREntity::IComponentDataImpl<C_Static>;

    std::uint64_t settleStamp_ = 0;
    static_assert(std::is_same_v<decltype(settleStamp_), std::uint64_t>);
};

/// Engine-owned: the deriving systems have run over the entity's frozen
/// inputs. Never exists without `C_Static`, and never survives an archetype
/// move that does not name it. Only `IRSystem::promoteSettled` attaches it.
struct C_Settled {};

} // namespace IRComponents

namespace IREntity {

template <> inline constexpr bool kClearedByForeignMove<IRComponents::C_Settled> = true;
// A static entity's world transform depends on its parent chain.
template <> inline constexpr bool kTracksInputChanges<IRComponents::C_Static> = true;

template <> struct EngineAttach<IRComponents::C_Settled> {
    static constexpr bool kRestricted = true;
    using KeyOwner = IRSystem::detail::SettlePromoter;
    using Companion = IRComponents::C_Static;
};

} // namespace IREntity

#endif /* COMPONENT_STATIC_H */
