#ifndef MODIFIER_APPLY_H
#define MODIFIER_APPLY_H

// Generic factory for the inline-apply pattern: compose the entity's
// vec3 modifiers for one field against a vec3(0) base and ADD the
// result into a vec3 member of `TargetComponent`. Globals are NOT
// folded in here — that's the sharpest discriminator vs. the
// structured-resolver path. See docs/design/modifiers.md "Inline-apply
// pattern" for the full discriminator and rationale.
//
// `Member` is a member-pointer (compile-time non-type template param)
// so the per-tick body is `target.*Member += compose(...)` with no
// field-name lookup or branching. `field` is a runtime arg because
// `FieldBindingId`s register lazily (via `position_modifier_fields.hpp`
// and friends) and forcing callers to make their ids `constexpr` would
// break that.

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
