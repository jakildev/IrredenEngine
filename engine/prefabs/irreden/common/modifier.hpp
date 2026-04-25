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

inline const char *fieldName(IRComponents::FieldBindingId id) {
    return detail::globalFieldRegistry().fieldName(id);
}

inline std::size_t fieldCount() {
    return detail::globalFieldRegistry().fieldCount();
}

// Push a structured modifier onto the target entity. Defensively rejects
// kInvalidFieldId so a default-constructed Modifier{} can't slip into
// the pipeline.
inline void push(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    float param,
    IREntity::EntityId source,
    std::int32_t ticksRemaining = -1
) {
    if (field == IRComponents::kInvalidFieldId) return;
    auto *c = IREntity::getComponentOptional<IRComponents::C_Modifiers>(target);
    if (!c) return;
    c->modifiers_.push_back(IRComponents::Modifier{
        field, kind, param, source, ticksRemaining
    });
}

inline void pushGlobal(
    IRComponents::FieldBindingId field,
    IRComponents::TransformKind kind,
    float param,
    IREntity::EntityId source,
    std::int32_t ticksRemaining = -1
) {
    if (field == IRComponents::kInvalidFieldId) return;
    auto entity = detail::globalsEntityId();
    if (entity == IREntity::kNullEntity) return;
    auto *c = IREntity::getComponentOptional<IRComponents::C_GlobalModifiers>(entity);
    if (!c) return;
    c->modifiers_.push_back(IRComponents::Modifier{
        field, kind, param, source, ticksRemaining
    });
}

inline void pushLambda(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    std::function<float(float)> fn,
    IREntity::EntityId source,
    std::int32_t ticksRemaining = -1
) {
    if (field == IRComponents::kInvalidFieldId) return;
    auto *c = IREntity::getComponentOptional<IRComponents::C_LambdaModifiers>(target);
    if (!c) return;
    c->modifiers_.push_back(IRComponents::LambdaModifier{
        field, std::move(fn), source, ticksRemaining
    });
}

// Sweep both per-entity and global modifier vectors, removing any whose
// source_ matches `source`. Linear in the total number of (entity ×
// modifier) pairs in the world. Engine consumers call this from a
// pre-destroy hook on the source's owning system; for v1, callers must
// invoke explicitly before destroying the source entity.
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

    IREntity::forEachComponent<IRComponents::C_Modifiers>(
        [&](IRComponents::C_Modifiers &c) { stripVec(c.modifiers_); }
    );
    IREntity::forEachComponent<IRComponents::C_GlobalModifiers>(
        [&](IRComponents::C_GlobalModifiers &c) { stripVec(c.modifiers_); }
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
inline float applyToField(
    IREntity::EntityId target,
    IRComponents::FieldBindingId field,
    float baseValue
) {
    static const std::vector<IRComponents::Modifier> kEmpty;
    auto *c = IREntity::getComponentOptional<IRComponents::C_Modifiers>(target);
    const auto &entityMods = c ? c->modifiers_ : kEmpty;

    const std::vector<IRComponents::Modifier> *globalsPtr = nullptr;
    auto entity = detail::globalsEntityId();
    if (entity != IREntity::kNullEntity) {
        auto *g = IREntity::getComponentOptional<IRComponents::C_GlobalModifiers>(entity);
        if (g) globalsPtr = &g->modifiers_;
    }
    const auto &globals = globalsPtr ? *globalsPtr : kEmpty;

    return detail::composeForField(baseValue, field, globals, entityMods);
}

// Wires up the singleton globals entity and registers the four resolver
// systems in the canonical order. Must be called once at creation init,
// before any system that depends on resolved fields.
//
// Pipeline ordering chosen by the caller's registerPipeline(); this
// function returns the SystemIds in resolver order so the caller can
// splice them in.
struct ResolverPipelineSystems {
    IRSystem::SystemId modifierDecay_;
    IRSystem::SystemId globalModifierDecay_;
    IRSystem::SystemId modifierResolveGlobal_;
    IRSystem::SystemId modifierResolveLambda_;
};

inline ResolverPipelineSystems registerResolverPipeline() {
    auto &globalsEntity = detail::globalsEntityId();
    if (globalsEntity == IREntity::kNullEntity) {
        globalsEntity = IREntity::createEntity(
            IRComponents::C_GlobalModifiers{}
        );
        IREntity::setName(globalsEntity, "modifierGlobals");
    }
    return ResolverPipelineSystems{
        IRSystem::createSystem<IRSystem::MODIFIER_DECAY>(),
        IRSystem::createSystem<IRSystem::GLOBAL_MODIFIER_DECAY>(),
        IRSystem::createSystem<IRSystem::MODIFIER_RESOLVE_GLOBAL>(),
        IRSystem::createSystem<IRSystem::MODIFIER_RESOLVE_LAMBDA>(),
    };
}

inline IREntity::EntityId globalsEntity() {
    return detail::globalsEntityId();
}

} // namespace IRPrefab::Modifier

#endif /* MODIFIER_H */
