#ifndef MODIFIER_APPLY_H
#define MODIFIER_APPLY_H

// Generic factory for the inline-apply pattern: compose the entity's
// vec3 modifiers for one field against a vec3(0) base and ADD the
// result into a vec3 member of `TargetComponent`. Skips the
// `C_ResolvedFields` cache and the resolver pipeline — the right shape
// when a vec3 modifier channel has exactly one consumer and caching
// would be pure overhead.
//
// Members of the family (anything that is "per-frame additive delta on
// one vec3 field, written into one vec3 component member") collapse to
// a single template instantiation:
//
//   template <> struct System<APPLY_POSITION_OFFSET> {
//       static SystemId create() {
//           return IRPrefab::Modifier::applyVec3ModifierTo<
//               IRComponents::C_PositionGlobal3D,
//               &IRComponents::C_PositionGlobal3D::pos_>(
//               "ApplyPositionOffset",
//               IRPrefab::PositionModifier::positionOffsetField()
//           );
//       }
//   };
//
// Picking inline-apply vs. structured-resolver:
//
// - **Inline-apply (this helper):** exactly one consumer of the vec3
//   field, no global modifiers needed for the channel, ADD-onto-target
//   semantics. The compose runs once per entity per frame and writes
//   the destination directly. No `C_ResolvedFields` slot consumed.
//
// - **Structured-resolver path (`MODIFIER_RESOLVE_*` + read from
//   `C_ResolvedFields`):** two or more consumers want the same
//   resolved value, the channel needs `C_GlobalModifiers` integration,
//   or the apply step is multiplicative / set-style (e.g.
//   `TRANSFORM_SCALE`). The cache amortizes the compose across
//   readers and the global-modifier archetype routing falls out for
//   free.
//
// Globals integration: the inline path reads the entity's modifier
// vector only; `C_GlobalModifiers` are not folded in. Channels that
// must respond to globals belong on the structured-resolver path.
//
// `Member` is a member-pointer (compile-time) so the per-tick body is
// just `target.*Member += compose(...)` — no field-name lookup, no
// branching on which vec3 field of the component is being written.
// `field` is captured at create() time; `FieldBindingId`s register
// dynamically (lazy via `position_modifier_fields.hpp` and friends),
// so a runtime arg is the correct binding point.

#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/modifier_compose.hpp>

namespace IRPrefab::Modifier {

template <typename TargetComponent, IRMath::vec3 TargetComponent::*Member>
inline IRSystem::SystemId applyVec3ModifierTo(
    const char *name,
    IRComponents::FieldBindingId field
) {
    return IRSystem::createSystem<TargetComponent, IRComponents::C_Modifiers>(
        name,
        [field](TargetComponent &target, IRComponents::C_Modifiers &mods) {
            target.*Member += IRPrefab::Modifier::detail::composeForFieldVec3(
                IRMath::vec3(0.0f),
                field,
                mods.modifiersVec3_
            );
        }
    );
}

} // namespace IRPrefab::Modifier

#endif /* MODIFIER_APPLY_H */
