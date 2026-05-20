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
// Entities tagged with C_NoGlobalModifiers are routed away by the
// archetype filter (Exclude<C_NoGlobalModifiers>) so they fall through
// to MODIFIER_RESOLVE_EXEMPT instead — no per-entity branching here.

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

inline const std::vector<IRComponents::ModifierVec3> *&currentGlobalModifiersVec3Ptr() {
    static const std::vector<IRComponents::ModifierVec3> *p = nullptr;
    return p;
}

inline const std::vector<IRComponents::ModifierQuat> *&currentGlobalModifiersQuatPtr() {
    static const std::vector<IRComponents::ModifierQuat> *p = nullptr;
    return p;
}

// No-create accessor for the singleton globals entity. Returns
// `kNullEntity` if `registerResolverPipeline` has not yet wired up
// the singleton, so `beginTick` below can no-op cleanly when the
// modifier framework is registered later in init order than this
// system's first tick. Use `IREntity::singletonEntity<C_GlobalModifiers>`
// directly when you intend to lazy-create.
inline IREntity::EntityId globalsEntityId() {
    using IRComponents::C_GlobalModifiers;
    return IREntity::singletonEntityOrNull<C_GlobalModifiers>();
}

} // namespace IRPrefab::Modifier::detail

namespace IRSystem {

template <> struct System<MODIFIER_RESOLVE_GLOBAL> {
    static SystemId create() {
        return createSystem<
            IRComponents::C_Modifiers,
            IRComponents::C_ResolvedFields,
            Exclude<IRComponents::C_NoGlobalModifiers>>(
            "ModifierResolveGlobal",
            [](IRComponents::C_Modifiers &m, IRComponents::C_ResolvedFields &resolved) {
                const auto *globalsPtr = IRPrefab::Modifier::detail::currentGlobalModifiersPtr();
                const auto &globals =
                    globalsPtr ? *globalsPtr : IRPrefab::Modifier::detail::emptyModifiers();
                for (auto &rf : resolved.fields_) {
                    rf.value_ = IRPrefab::Modifier::detail::composeForField(
                        rf.value_,
                        rf.field_,
                        globals,
                        m.modifiers_
                    );
                }
                const auto *globalsVec3Ptr =
                    IRPrefab::Modifier::detail::currentGlobalModifiersVec3Ptr();
                const auto &globalsVec3 = globalsVec3Ptr
                                              ? *globalsVec3Ptr
                                              : IRPrefab::Modifier::detail::emptyModifiersVec3();
                for (auto &rf : resolved.fieldsVec3_) {
                    rf.value_ = IRPrefab::Modifier::detail::composeForFieldVec3(
                        rf.value_,
                        rf.field_,
                        globalsVec3,
                        m.modifiersVec3_
                    );
                }
                const auto *globalsQuatPtr =
                    IRPrefab::Modifier::detail::currentGlobalModifiersQuatPtr();
                const auto &globalsQuat = globalsQuatPtr
                                              ? *globalsQuatPtr
                                              : IRPrefab::Modifier::detail::emptyModifiersQuat();
                for (auto &rf : resolved.fieldsQuat_) {
                    rf.value_ = IRPrefab::Modifier::detail::composeForFieldQuat(
                        rf.value_,
                        rf.field_,
                        globalsQuat,
                        m.modifiersQuat_
                    );
                }
            },
            // beginTick: cache the singleton's modifier vector pointers.
            []() {
                using IRComponents::C_GlobalModifiers;
                auto &cachedPtr = IRPrefab::Modifier::detail::currentGlobalModifiersPtr();
                auto &cachedVec3Ptr = IRPrefab::Modifier::detail::currentGlobalModifiersVec3Ptr();
                auto &cachedQuatPtr = IRPrefab::Modifier::detail::currentGlobalModifiersQuatPtr();
                auto entity = IRPrefab::Modifier::detail::globalsEntityId();
                if (entity == IREntity::kNullEntity) {
                    cachedPtr = nullptr;
                    cachedVec3Ptr = nullptr;
                    cachedQuatPtr = nullptr;
                    return;
                }
                auto *gm =
                    IREntity::getComponentOptional<C_GlobalModifiers>(entity).value_or(nullptr);
                cachedPtr = gm ? &gm->modifiers_ : nullptr;
                cachedVec3Ptr = gm ? &gm->modifiersVec3_ : nullptr;
                cachedQuatPtr = gm ? &gm->modifiersQuat_ : nullptr;
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_MODIFIER_RESOLVE_GLOBAL_H */
