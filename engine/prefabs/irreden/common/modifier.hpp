#ifndef MODIFIER_H
#define MODIFIER_H

// Public API for the modifier framework — IRPrefab::Modifier:: namespace.
// Free functions only; the framework is engine-level state owned by the
// global field registry, the singleton globals entity, and the resolver
// systems registered in registerResolverPipeline().
//
// Per the engine prefab layering principle, this lives at the prefab
// layer (NOT in IRRender:: or any engine-library namespace). See
// engine/prefabs/irreden/render/CLAUDE.md §"Exposing system public API
// from the prefab layer".
//
// See docs/design/modifiers.md for the locked design and resolver
// evaluation order.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/modifier_compose.hpp>
#include <irreden/common/modifier_field_registry.hpp>
#include <irreden/common/systems/system_global_modifier_decay.hpp>
#include <irreden/common/systems/system_modifier_decay.hpp>
#include <irreden/common/systems/system_modifier_lambda_decay.hpp>
#include <irreden/common/systems/system_modifier_resolve_exempt.hpp>
#include <irreden/common/systems/system_modifier_resolve_global.hpp>
#include <irreden/common/systems/system_modifier_resolve_lambda.hpp>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace IRPrefab::Modifier {

// Field registry — call at init from feature owners. `name` must have
// static-storage lifetime (use a string literal); the registry stores
// the pointer, not a copy. Registration is single-threaded.
inline IRComponents::FieldBindingId registerField(const char *name) {
    return detail::globalFieldRegistry().registerField(name);
}

// vec3-typed field. Pushes against this id must use the vec3 push()
// overloads; the scalar-typed overloads silently no-op for vec3 fields
// (caller bug, not data error).
inline IRComponents::FieldBindingId registerFieldVec3(const char *name) {
    return detail::globalFieldRegistry().registerFieldVec3(name);
}

// Quat-typed field. Pushes must use the vec4 push() overloads and the
// quaternion compose semantics (MULTIPLY = left-multiply, OVERRIDE/SET
// replace; ADD/CLAMP_MIN/CLAMP_MAX assert).
inline IRComponents::FieldBindingId registerFieldQuat(const char *name) {
    return detail::globalFieldRegistry().registerFieldQuat(name);
}

inline const char *fieldName(IRComponents::FieldBindingId id) {
    return detail::globalFieldRegistry().fieldName(id);
}

inline IRComponents::FieldValueType fieldType(IRComponents::FieldBindingId id) {
    return detail::globalFieldRegistry().fieldType(id);
}

inline std::size_t fieldCount() {
    return detail::globalFieldRegistry().fieldCount();
}

// Push a structured modifier onto the target entity. Defensively rejects
// kInvalidFieldId so a default-constructed Modifier{} can't slip into
// the pipeline. Also rejects mismatched typing (pushing a scalar against
// a vec3-registered field, or vice versa) so the resolver sees one type
// per field.
inline void push(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    float param,
    IREntity::EntityId source,
    std::int32_t ticksRemaining = -1
) {
    if (field == IRComponents::kInvalidFieldId)
        return;
    if (fieldType(field) != IRComponents::FieldValueType::SCALAR)
        return;
    auto *c = IREntity::getComponentOptional<IRComponents::C_Modifiers>(target).value_or(nullptr);
    if (!c)
        return;
    c->modifiers_.push_back(IRComponents::Modifier{field, kind, param, source, ticksRemaining});
}

inline void push(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    IRMath::vec3 param,
    IREntity::EntityId source,
    std::int32_t ticksRemaining = -1
) {
    if (field == IRComponents::kInvalidFieldId)
        return;
    if (fieldType(field) != IRComponents::FieldValueType::VEC3)
        return;
    auto *c = IREntity::getComponentOptional<IRComponents::C_Modifiers>(target).value_or(nullptr);
    if (!c)
        return;
    c->modifiersVec3_.push_back(
        IRComponents::ModifierVec3{field, kind, param, source, ticksRemaining}
    );
}

// Quat push. `param` is the engine's canonical quaternion layout
// (`vec4(qx, qy, qz, qw)`, identity `vec4(0, 0, 0, 1)`). ADD / CLAMP_MIN /
// CLAMP_MAX fire `IR_ASSERT` in debug and skip in release — they are
// not meaningful operations on a unit quaternion. MULTIPLY composes
// left-multiply (post-rotate); OVERRIDE/SET replace.
inline void push(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    IRMath::vec4 param,
    IREntity::EntityId source,
    std::int32_t ticksRemaining = -1
) {
    if (field == IRComponents::kInvalidFieldId)
        return;
    if (fieldType(field) != IRComponents::FieldValueType::QUAT)
        return;
    const bool unitQuatIncompatible = kind == IRComponents::TransformKind::ADD ||
                                      kind == IRComponents::TransformKind::CLAMP_MIN ||
                                      kind == IRComponents::TransformKind::CLAMP_MAX;
    IR_ASSERT(
        !unitQuatIncompatible,
        "Quat modifier kind: ADD/CLAMP_MIN/CLAMP_MAX are not meaningful on unit "
        "quaternions; use MULTIPLY for compose, OVERRIDE/SET for replacement."
    );
    if (unitQuatIncompatible)
        return;
    auto *c = IREntity::getComponentOptional<IRComponents::C_Modifiers>(target).value_or(nullptr);
    if (!c)
        return;
    c->modifiersQuat_.push_back(
        IRComponents::ModifierQuat{field, kind, param, source, ticksRemaining}
    );
}

// Caller runs INSIDE the resolver pipeline, after MODIFIER_DECAY in the same
// UPDATE tick. Modifier survives this frame's compose; next frame's DECAY
// removes it. Use for system ticks positioned inside the pipeline.
inline void pushFrameLocal(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    float param,
    IREntity::EntityId source
) {
    push(target, field, kind, param, source, 1);
}

inline void pushFrameLocal(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    IRMath::vec3 param,
    IREntity::EntityId source
) {
    push(target, field, kind, param, source, 1);
}

// Caller runs OUTSIDE the resolver pipeline (Lua, input handler, command).
// Modifier survives the next frame's DECAY + compose; the frame after removes
// it. The extra tick accounts for DECAY running before the first compose.
inline void pushOneFrame(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    float param,
    IREntity::EntityId source
) {
    push(target, field, kind, param, source, 2);
}

inline void pushOneFrame(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    IRMath::vec3 param,
    IREntity::EntityId source
) {
    push(target, field, kind, param, source, 2);
}

inline void pushGlobal(
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    float param,
    IREntity::EntityId source,
    std::int32_t ticksRemaining = -1
) {
    if (field == IRComponents::kInvalidFieldId)
        return;
    if (fieldType(field) != IRComponents::FieldValueType::SCALAR)
        return;
    auto entity = detail::globalsEntityId();
    if (entity == IREntity::kNullEntity)
        return;
    auto *c =
        IREntity::getComponentOptional<IRComponents::C_GlobalModifiers>(entity).value_or(nullptr);
    if (!c)
        return;
    c->modifiers_.push_back(IRComponents::Modifier{field, kind, param, source, ticksRemaining});
}

inline void pushGlobal(
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    IRMath::vec3 param,
    IREntity::EntityId source,
    std::int32_t ticksRemaining = -1
) {
    if (field == IRComponents::kInvalidFieldId)
        return;
    if (fieldType(field) != IRComponents::FieldValueType::VEC3)
        return;
    auto entity = detail::globalsEntityId();
    if (entity == IREntity::kNullEntity)
        return;
    auto *c =
        IREntity::getComponentOptional<IRComponents::C_GlobalModifiers>(entity).value_or(nullptr);
    if (!c)
        return;
    c->modifiersVec3_.push_back(
        IRComponents::ModifierVec3{field, kind, param, source, ticksRemaining}
    );
}

inline void pushGlobal(
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    IRMath::vec4 param,
    IREntity::EntityId source,
    std::int32_t ticksRemaining = -1
) {
    if (field == IRComponents::kInvalidFieldId)
        return;
    if (fieldType(field) != IRComponents::FieldValueType::QUAT)
        return;
    const bool unitQuatIncompatible = kind == IRComponents::TransformKind::ADD ||
                                      kind == IRComponents::TransformKind::CLAMP_MIN ||
                                      kind == IRComponents::TransformKind::CLAMP_MAX;
    IR_ASSERT(
        !unitQuatIncompatible,
        "Quat global modifier kind: ADD/CLAMP_MIN/CLAMP_MAX are not meaningful on unit "
        "quaternions; use MULTIPLY for compose, OVERRIDE/SET for replacement."
    );
    if (unitQuatIncompatible)
        return;
    auto entity = detail::globalsEntityId();
    if (entity == IREntity::kNullEntity)
        return;
    auto *c =
        IREntity::getComponentOptional<IRComponents::C_GlobalModifiers>(entity).value_or(nullptr);
    if (!c)
        return;
    c->modifiersQuat_.push_back(
        IRComponents::ModifierQuat{field, kind, param, source, ticksRemaining}
    );
}

// `ticksRemaining` is honored by `LAMBDA_MODIFIER_DECAY`, registered alongside
// the structured-modifier decay systems in `registerResolverPipeline`. Decay
// runs at the start of the resolver pipeline, so a value of 1 fires for zero
// frames; use 2 to fire for exactly one frame. `-1` is the sentinel for
// "no decay" (modifier persists until manually removed via `removeBySource`).
inline void pushLambda(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    std::function<float(float)> fn,
    IREntity::EntityId source,
    std::int32_t ticksRemaining = -1
) {
    if (field == IRComponents::kInvalidFieldId)
        return;
    auto *c =
        IREntity::getComponentOptional<IRComponents::C_LambdaModifiers>(target).value_or(nullptr);
    if (!c)
        return;
    c->modifiers_.push_back(
        IRComponents::LambdaModifier{field, std::move(fn), source, ticksRemaining}
    );
}

// Sweep both per-entity and global modifier vectors, removing any whose
// source_ matches `source`. Sweeps scalar AND vec3 vectors on every
// component touched. Linear in the total number of (entity ×
// modifier) pairs in the world. Wired automatically into
// `EntityManager::destroyEntity` by `registerResolverPipeline()` — a
// pre-destroy hook fires `removeBySource(destroyedEntity)` before the
// EntityId is recycled. Callers who want to drop a source's modifiers
// without destroying the source (e.g. "ability ends but caster
// persists") still invoke this directly.
inline void removeBySource(IREntity::EntityId source) {
    auto stripVec = [&](auto &v) {
        v.erase(
            std::remove_if(
                v.begin(),
                v.end(),
                [&](const auto &mod) { return mod.source_ == source; }
            ),
            v.end()
        );
    };

    IREntity::forEachComponent<IRComponents::C_Modifiers>([&](IRComponents::C_Modifiers &c) {
        stripVec(c.modifiers_);
        stripVec(c.modifiersVec3_);
        stripVec(c.modifiersQuat_);
    });
    IREntity::forEachComponent<IRComponents::C_GlobalModifiers>(
        [&](IRComponents::C_GlobalModifiers &c) {
            stripVec(c.modifiers_);
            stripVec(c.modifiersVec3_);
            stripVec(c.modifiersQuat_);
        }
    );
    IREntity::forEachComponent<IRComponents::C_LambdaModifiers>(
        [&](IRComponents::C_LambdaModifiers &c) { stripVec(c.modifiers_); }
    );
}

// Direct query — composes the entity's structured modifiers (and the
// global vector) on top of `baseValue` and returns the resolved value
// without touching C_ResolvedFields. The resolver pipeline and
// applyToField share `composeForField`, so the two read paths give
// the same answer for the same input.
inline float
applyToField(IREntity::EntityId target, IRComponents::FieldBindingId field, float baseValue) {
    auto *c = IREntity::getComponentOptional<IRComponents::C_Modifiers>(target).value_or(nullptr);
    const auto &entityMods = c ? c->modifiers_ : detail::emptyModifiers();

    const std::vector<IRComponents::Modifier> *globalsPtr = nullptr;
    auto entity = detail::globalsEntityId();
    if (entity != IREntity::kNullEntity) {
        auto *g = IREntity::getComponentOptional<IRComponents::C_GlobalModifiers>(entity).value_or(
            nullptr
        );
        if (g)
            globalsPtr = &g->modifiers_;
    }
    const auto &globals = globalsPtr ? *globalsPtr : detail::emptyModifiers();

    return detail::composeForField(baseValue, field, globals, entityMods);
}

// vec3 counterpart of `applyToField`. Same direct-query semantics; reads
// from the vec3 modifier vectors on C_Modifiers and C_GlobalModifiers.
inline IRMath::vec3 applyToFieldVec3(
    IREntity::EntityId target, IRComponents::FieldBindingId field, IRMath::vec3 baseValue
) {
    auto *c = IREntity::getComponentOptional<IRComponents::C_Modifiers>(target).value_or(nullptr);
    const auto &entityMods = c ? c->modifiersVec3_ : detail::emptyModifiersVec3();

    const std::vector<IRComponents::ModifierVec3> *globalsPtr = nullptr;
    auto entity = detail::globalsEntityId();
    if (entity != IREntity::kNullEntity) {
        auto *g = IREntity::getComponentOptional<IRComponents::C_GlobalModifiers>(entity).value_or(
            nullptr
        );
        if (g)
            globalsPtr = &g->modifiersVec3_;
    }
    const auto &globals = globalsPtr ? *globalsPtr : detail::emptyModifiersVec3();

    return detail::composeForFieldVec3(baseValue, field, globals, entityMods);
}

// Quat counterpart of `applyToField`. Reads from `modifiersQuat_` on
// `C_Modifiers` and the singleton's `C_GlobalModifiers`; returns the
// composed (and normalized, when any modifier touched the value)
// rotation. `baseValue` defaults to identity at the caller site — pass
// `vec4(0, 0, 0, 1)` if you have no other base rotation in mind.
inline IRMath::vec4 applyToFieldQuat(
    IREntity::EntityId target, IRComponents::FieldBindingId field, IRMath::vec4 baseValue
) {
    auto *c = IREntity::getComponentOptional<IRComponents::C_Modifiers>(target).value_or(nullptr);
    const auto &entityMods = c ? c->modifiersQuat_ : detail::emptyModifiersQuat();

    const std::vector<IRComponents::ModifierQuat> *globalsPtr = nullptr;
    auto entity = detail::globalsEntityId();
    if (entity != IREntity::kNullEntity) {
        auto *g = IREntity::getComponentOptional<IRComponents::C_GlobalModifiers>(entity).value_or(
            nullptr
        );
        if (g)
            globalsPtr = &g->modifiersQuat_;
    }
    const auto &globals = globalsPtr ? *globalsPtr : detail::emptyModifiersQuat();

    return detail::composeForFieldQuat(baseValue, field, globals, entityMods);
}

// Upsert a steady-state scalar modifier keyed on the triple
// (source, field, kind). On hit: overwrite param_ and reset ticksRemaining_
// to -1 (no decay). On miss: push_back with ticksRemaining_ = -1.
// Performs the same defensive field-type and kInvalidFieldId checks as push().
// Use when one source writes the same slot every tick forever (e.g. an idle
// bob driven by a tick), so the vector holds exactly one entry per slot
// instead of growing without bound.
inline void upsertBySource(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    float param,
    IREntity::EntityId source
) {
    if (field == IRComponents::kInvalidFieldId)
        return;
    if (fieldType(field) != IRComponents::FieldValueType::SCALAR)
        return;
    auto *c = IREntity::getComponentOptional<IRComponents::C_Modifiers>(target).value_or(nullptr);
    if (!c)
        return;
    for (auto &m : c->modifiers_) {
        if (m.source_ == source && m.field_ == field && m.kind_ == kind) {
            m.param_ = param;
            m.ticksRemaining_ = -1;
            return;
        }
    }
    c->modifiers_.push_back(IRComponents::Modifier{field, kind, param, source, -1});
}

// vec3 counterpart of upsertBySource. Slot key is (source, field, kind).
inline void upsertBySource(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    IRMath::vec3 param,
    IREntity::EntityId source
) {
    if (field == IRComponents::kInvalidFieldId)
        return;
    if (fieldType(field) != IRComponents::FieldValueType::VEC3)
        return;
    auto *c = IREntity::getComponentOptional<IRComponents::C_Modifiers>(target).value_or(nullptr);
    if (!c)
        return;
    for (auto &m : c->modifiersVec3_) {
        if (m.source_ == source && m.field_ == field && m.kind_ == kind) {
            m.param_ = param;
            m.ticksRemaining_ = -1;
            return;
        }
    }
    c->modifiersVec3_.push_back(IRComponents::ModifierVec3{field, kind, param, source, -1});
}

// upsertBySource targeting the singleton globals entity (scalar).
inline void upsertBySourceGlobal(
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    float param,
    IREntity::EntityId source
) {
    if (field == IRComponents::kInvalidFieldId)
        return;
    if (fieldType(field) != IRComponents::FieldValueType::SCALAR)
        return;
    auto entity = detail::globalsEntityId();
    if (entity == IREntity::kNullEntity)
        return;
    auto *c =
        IREntity::getComponentOptional<IRComponents::C_GlobalModifiers>(entity).value_or(nullptr);
    if (!c)
        return;
    for (auto &m : c->modifiers_) {
        if (m.source_ == source && m.field_ == field && m.kind_ == kind) {
            m.param_ = param;
            m.ticksRemaining_ = -1;
            return;
        }
    }
    c->modifiers_.push_back(IRComponents::Modifier{field, kind, param, source, -1});
}

// upsertBySource targeting the singleton globals entity (vec3).
inline void upsertBySourceGlobal(
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    IRMath::vec3 param,
    IREntity::EntityId source
) {
    if (field == IRComponents::kInvalidFieldId)
        return;
    if (fieldType(field) != IRComponents::FieldValueType::VEC3)
        return;
    auto entity = detail::globalsEntityId();
    if (entity == IREntity::kNullEntity)
        return;
    auto *c =
        IREntity::getComponentOptional<IRComponents::C_GlobalModifiers>(entity).value_or(nullptr);
    if (!c)
        return;
    for (auto &m : c->modifiersVec3_) {
        if (m.source_ == source && m.field_ == field && m.kind_ == kind) {
            m.param_ = param;
            m.ticksRemaining_ = -1;
            return;
        }
    }
    c->modifiersVec3_.push_back(IRComponents::ModifierVec3{field, kind, param, source, -1});
}

// In-place scalar upsert for system ticks that already hold C_Modifiers& from
// archetype iteration. Skips the getComponentOptional probe and the field-type
// check — caller (a system) already knows the field id from init-time
// registration. Same slot key and overwrite semantics as upsertBySource.
inline void upsertBySourceInPlace(
    IRComponents::C_Modifiers &mods,
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    float param,
    IREntity::EntityId source
) {
    for (auto &m : mods.modifiers_) {
        if (m.source_ == source && m.field_ == field && m.kind_ == kind) {
            m.param_ = param;
            m.ticksRemaining_ = -1;
            return;
        }
    }
    mods.modifiers_.push_back(IRComponents::Modifier{field, kind, param, source, -1});
}

// In-place vec3 upsert for system ticks that already hold C_Modifiers&.
inline void upsertBySourceInPlace(
    IRComponents::C_Modifiers &mods,
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    IRMath::vec3 param,
    IREntity::EntityId source
) {
    for (auto &m : mods.modifiersVec3_) {
        if (m.source_ == source && m.field_ == field && m.kind_ == kind) {
            m.param_ = param;
            m.ticksRemaining_ = -1;
            return;
        }
    }
    mods.modifiersVec3_.push_back(IRComponents::ModifierVec3{field, kind, param, source, -1});
}

// Wires up the singleton globals entity and registers the six resolver
// systems in the canonical order. Must be called once at creation init,
// before any system that depends on resolved fields.
//
// Pipeline ordering chosen by the caller's registerPipeline(); this
// function returns the SystemIds in resolver order so the caller can
// splice them in. The two RESOLVE_GLOBAL/RESOLVE_EXEMPT systems
// archetype-route on C_NoGlobalModifiers so each entity hits exactly
// one (no per-entity branching).
struct ResolverPipelineSystems {
    IRSystem::SystemId modifierDecay_;
    IRSystem::SystemId globalModifierDecay_;
    IRSystem::SystemId lambdaModifierDecay_;
    IRSystem::SystemId modifierResolveGlobal_;
    IRSystem::SystemId modifierResolveExempt_;
    IRSystem::SystemId modifierResolveLambda_;
};

inline ResolverPipelineSystems registerResolverPipeline() {
    static bool registered = false;
    IR_ASSERT(
        !registered,
        "registerResolverPipeline called more than once — duplicate decay/resolve systems would "
        "double-apply per tick"
    );
    registered = true;
    // Name the singleton globals entity so tooling that scans named
    // entities (and `globalsEntity()` below) can find it by lookup
    // rather than by re-resolving the singleton.
    auto globalsEntity = IREntity::singletonEntity<IRComponents::C_GlobalModifiers>();
    IREntity::setName(globalsEntity, "modifierGlobals");
    // Auto-sweep source-attributed modifiers when their source entity is
    // destroyed. Without this, a target outliving its source keeps the
    // dead source's modifiers applied indefinitely; recycled EntityIds
    // would then inherit them on a future allocation. Linear sweep is
    // acceptable for v1 — see CLAUDE.md "Open follow-ups" for the
    // reverse-index option if churn becomes a profile hotspot.
    (void)IREntity::getEntityManager().registerPreDestroyHook([](IREntity::EntityId destroyed) {
        removeBySource(destroyed);
    });
    return ResolverPipelineSystems{
        IRSystem::createSystem<IRSystem::MODIFIER_DECAY>(),
        IRSystem::createSystem<IRSystem::GLOBAL_MODIFIER_DECAY>(),
        IRSystem::createSystem<IRSystem::LAMBDA_MODIFIER_DECAY>(),
        IRSystem::createSystem<IRSystem::MODIFIER_RESOLVE_GLOBAL>(),
        IRSystem::createSystem<IRSystem::MODIFIER_RESOLVE_EXEMPT>(),
        IRSystem::createSystem<IRSystem::MODIFIER_RESOLVE_LAMBDA>(),
    };
}

// For tests and diagnostics only; production code should use pushGlobal /
// removeBySource rather than touching the entity directly.
inline IREntity::EntityId globalsEntity() {
    return detail::globalsEntityId();
}

} // namespace IRPrefab::Modifier

#endif /* MODIFIER_H */
