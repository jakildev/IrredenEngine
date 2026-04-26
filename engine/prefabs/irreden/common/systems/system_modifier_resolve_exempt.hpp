#ifndef SYSTEM_MODIFIER_RESOLVE_EXEMPT_H
#define SYSTEM_MODIFIER_RESOLVE_EXEMPT_H

// Sibling of MODIFIER_RESOLVE_GLOBAL for entities tagged
// C_NoGlobalModifiers. Composes only the entity's own C_Modifiers
// (skipping the global vector) into C_ResolvedFields.
//
// Dispatch is archetype-routed: this system's include archetype
// requires C_NoGlobalModifiers, while MODIFIER_RESOLVE_GLOBAL excludes
// the same tag. Together they partition the C_Modifiers + C_ResolvedFields
// population without a per-entity branch.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/modifier_compose.hpp>

namespace IRSystem {

template <> struct System<MODIFIER_RESOLVE_EXEMPT> {
    static SystemId create() {
        return createSystem<
            IRComponents::C_Modifiers,
            IRComponents::C_ResolvedFields,
            IRComponents::C_NoGlobalModifiers
        >(
            "ModifierResolveExempt",
            [](IRComponents::C_Modifiers &m,
               IRComponents::C_ResolvedFields &resolved,
               IRComponents::C_NoGlobalModifiers &) {
                for (auto &rf : resolved.fields_) {
                    rf.value_ = IRPrefab::Modifier::detail::composeForField(
                        rf.value_, rf.field_, m.modifiers_
                    );
                }
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_MODIFIER_RESOLVE_EXEMPT_H */
