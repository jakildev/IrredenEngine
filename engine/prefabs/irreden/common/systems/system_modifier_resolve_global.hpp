#ifndef SYSTEM_MODIFIER_RESOLVE_GLOBAL_H
#define SYSTEM_MODIFIER_RESOLVE_GLOBAL_H

// Composes structured modifiers for entities carrying both C_Modifiers
// and C_ResolvedFields. The combined sequence is
// (globals ++ entity_mods); composeForField handles OVERRIDE / clamp
// ordering across the boundary.
//
// The singleton C_GlobalModifiers vector is captured once per pipeline
// execution via beginTick (one entity name lookup + one component fetch
// per frame, not per entity). The cached pointer lives in
// `IRPrefab::Modifier::detail::currentGlobalModifiersPtr()`.
//
// The consumer system seeds C_ResolvedFields[field].value_ with the
// current base value before this system runs; the resolver mutates
// value_ in place.
//
// NOTE — exempt path (C_NoGlobalModifiers) is not yet wired. A separate
// MODIFIER_RESOLVE_EXEMPT system would require an exclude-tag filter
// mechanism in engine/system. Until that lands, exempt entities still
// receive globals; tag them with absorbing-no-op globals or push through
// a custom path.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/modifier_compose.hpp>

#include <vector>

namespace IRPrefab::Modifier::detail {

// Cached pointer set at beginTick by the resolver system below; used by
// the per-entity tick body so the singleton lookup is paid once per frame.
inline const std::vector<IRComponents::Modifier> *&currentGlobalModifiersPtr() {
    static const std::vector<IRComponents::Modifier> *p = nullptr;
    return p;
}

inline IREntity::EntityId &globalsEntityId() {
    static IREntity::EntityId id = IREntity::kNullEntity;
    return id;
}

} // namespace IRPrefab::Modifier::detail

namespace IRSystem {

template <> struct System<MODIFIER_RESOLVE_GLOBAL> {
    static SystemId create() {
        return createSystem<
            IRComponents::C_Modifiers,
            IRComponents::C_ResolvedFields
        >(
            "ModifierResolveGlobal",
            [](IRComponents::C_Modifiers &m,
               IRComponents::C_ResolvedFields &resolved) {
                const auto *globalsPtr =
                    IRPrefab::Modifier::detail::currentGlobalModifiersPtr();
                const auto &globals = globalsPtr ? *globalsPtr
                                                 : IRPrefab::Modifier::detail::emptyModifiers();
                for (auto &rf : resolved.fields_) {
                    rf.value_ = IRPrefab::Modifier::detail::composeForField(
                        rf.value_, rf.field_, globals, m.modifiers_
                    );
                }
            },
            // beginTick: cache the singleton's modifier vector pointer.
            []() {
                using IRComponents::C_GlobalModifiers;
                auto &cachedPtr =
                    IRPrefab::Modifier::detail::currentGlobalModifiersPtr();
                auto entity = IRPrefab::Modifier::detail::globalsEntityId();
                if (entity == IREntity::kNullEntity) {
                    cachedPtr = nullptr;
                    return;
                }
                auto *gm = IREntity::getComponentOptional<C_GlobalModifiers>(entity).value_or(nullptr);
                cachedPtr = gm ? &gm->modifiers_ : nullptr;
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_MODIFIER_RESOLVE_GLOBAL_H */
