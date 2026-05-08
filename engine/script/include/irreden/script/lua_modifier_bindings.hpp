#ifndef LUA_MODIFIER_BINDINGS_H
#define LUA_MODIFIER_BINDINGS_H

// T-102: Lua bindings for the modifier framework.
//
// Lua surface:
//
//   IRModifier.Transform.{ADD, MULTIPLY, SET, CLAMP_MIN, CLAMP_MAX, OVERRIDE}
//   IRModifier.registerField(name)                          → FieldBindingId
//   IRModifier.fieldId(name)                                → FieldBindingId
//                                                            (kInvalidFieldId
//                                                            when not registered)
//   IRModifier.fieldName(id)                                → string | nil
//   IRModifier.add(target, fieldNameOrId, opts)
//   IRModifier.addGlobal(fieldNameOrId, opts)
//   IRModifier.addLambda(target, fieldNameOrId, fn, opts)
//   IRModifier.removeBySource(source)
//   IRModifier.applyToField(target, fieldNameOrId, base)    → float
//   IRModifier.resolved(target, fieldNameOrId, fallback?)   → float
//
// `target` / `source` are entity ids (Lua integers — the same shape as
// `arch.entityAt(i)` returns from a Lua-defined system, and the same
// shape as the `LuaEntity.entity` field on the entity-handle wrapper).
//
// `fieldNameOrId` accepts either:
//   - a string ("Hp.current"), resolved via the field registry's name
//     table at call time. The registry is populated by C++ feature
//     owners calling `IRPrefab::Modifier::registerField(...)` and by
//     Lua-defined components auto-registering their scalar fields at
//     `IRComponent.register` time.
//   - a numeric `FieldBindingId` (preferred for hot-path Lua: cache
//     the id once at init via `IRModifier.fieldId(name)` or read it
//     off a Lua-defined-component handle's `fields.<name>.bindingId`).
//
// `opts` for `add` / `addGlobal` is a table:
//   {
//       transform = IRModifier.Transform.X,
//       value = number,                   -- defaults to 0.0
//       source = entityId,                -- optional, defaults to kNullEntity
//       ticks = integer,                  -- optional, defaults to -1 (no decay)
//   }
//
// The C_Modifiers / C_GlobalModifiers / C_LambdaModifiers / C_ResolvedFields
// components themselves are NOT exposed to Lua as usertypes — Lua scripts
// drive the framework through the function surface above (push request,
// query resolved, sweep). This matches the C++ public API in
// `IRPrefab::Modifier::*` and keeps the per-tick boundary clean (no
// std::vector field exposed across the Lua/C++ line).

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_entity.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/modifier.hpp>
#include <irreden/common/modifier_field_registry.hpp>
#include <irreden/script/lua_script.hpp>

#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>

namespace IRScript::detail {

inline IRComponents::FieldBindingId
resolveFieldArg(const sol::object &arg, const std::string &caller) {
    if (arg.is<lua_Integer>()) {
        return static_cast<IRComponents::FieldBindingId>(arg.as<lua_Integer>());
    }
    if (arg.is<std::string>()) {
        const std::string name = arg.as<std::string>();
        const auto id = IRPrefab::Modifier::detail::globalFieldRegistry().findFieldId(name.c_str());
        if (id == IRComponents::kInvalidFieldId) {
            throw sol::error{
                caller + ": unknown field name '" + name +
                "' — register it via IRModifier.registerField or via a "
                "Lua component IRComponent.register before pushing modifiers"
            };
        }
        return id;
    }
    throw sol::error{
        caller + ": field argument must be a string name or a "
                 "FieldBindingId integer"
    };
}

inline IRComponents::TransformKind
resolveTransform(const sol::table &opts, const std::string &caller) {
    sol::optional<lua_Integer> kind = opts.get<sol::optional<lua_Integer>>("transform");
    if (!kind) {
        throw sol::error{caller + ": opts.transform is required (IRModifier.Transform.X)"};
    }
    if (*kind < static_cast<lua_Integer>(IRComponents::TransformKind::ADD) ||
        *kind > static_cast<lua_Integer>(IRComponents::TransformKind::OVERRIDE)) {
        throw sol::error{caller + ": opts.transform out of range"};
    }
    return static_cast<IRComponents::TransformKind>(*kind);
}

inline void bindModifierFramework(LuaScript &script) {
    sol::state &lua = script.lua();
    if (lua["IRModifier"].valid()) {
        return; // idempotent
    }
    sol::table api = lua.create_table();

    sol::table transform = lua.create_table();
    transform["ADD"] = static_cast<lua_Integer>(IRComponents::TransformKind::ADD);
    transform["MULTIPLY"] = static_cast<lua_Integer>(IRComponents::TransformKind::MULTIPLY);
    transform["SET"] = static_cast<lua_Integer>(IRComponents::TransformKind::SET);
    transform["CLAMP_MIN"] = static_cast<lua_Integer>(IRComponents::TransformKind::CLAMP_MIN);
    transform["CLAMP_MAX"] = static_cast<lua_Integer>(IRComponents::TransformKind::CLAMP_MAX);
    transform["OVERRIDE"] = static_cast<lua_Integer>(IRComponents::TransformKind::OVERRIDE);
    api["Transform"] = transform;

    api["kInvalidFieldId"] = static_cast<lua_Integer>(IRComponents::kInvalidFieldId);

    api["registerField"] = [](const std::string &name) {
        // The registry stores `const char*` without copying and assumes
        // static-storage lifetime (see modifier_field_registry.hpp). Lua
        // produces names at runtime, so we own them in a process-lifetime
        // deque (pointer-stable across emplace_back). Names registered
        // via `IRComponent.register` go through `luaFieldBindingNames()`
        // in lua_script.cpp; this deque is the parallel storage for
        // names registered directly via `IRModifier.registerField` —
        // both feed the same `IRPrefab::Modifier::registerField`.
        static std::deque<std::string> names;
        names.emplace_back(name);
        return static_cast<lua_Integer>(IRPrefab::Modifier::registerField(names.back().c_str()));
    };

    api["fieldId"] = [](const std::string &name) {
        return static_cast<lua_Integer>(
            IRPrefab::Modifier::detail::globalFieldRegistry().findFieldId(name.c_str())
        );
    };

    api["fieldName"] = [](sol::this_state L, lua_Integer id) -> sol::object {
        const char *name =
            IRPrefab::Modifier::fieldName(static_cast<IRComponents::FieldBindingId>(id));
        sol::state_view lua_view(L);
        if (name == nullptr) {
            return sol::make_object(lua_view, sol::lua_nil);
        }
        return sol::make_object(lua_view, std::string{name});
    };

    api["add"] = [](lua_Integer target, sol::object fieldArg, sol::table opts) {
        const auto field = resolveFieldArg(fieldArg, "IRModifier.add");
        const auto kind = resolveTransform(opts, "IRModifier.add");
        const float value = opts.get_or("value", 0.0f);
        const lua_Integer source =
            opts.get_or("source", static_cast<lua_Integer>(IREntity::kNullEntity));
        const std::int32_t ticks = opts.get_or("ticks", static_cast<std::int32_t>(-1));
        IRPrefab::Modifier::push(
            static_cast<IREntity::EntityId>(target),
            field,
            kind,
            value,
            static_cast<IREntity::EntityId>(source),
            ticks
        );
    };

    api["addGlobal"] = [](sol::object fieldArg, sol::table opts) {
        const auto field = resolveFieldArg(fieldArg, "IRModifier.addGlobal");
        const auto kind = resolveTransform(opts, "IRModifier.addGlobal");
        const float value = opts.get_or("value", 0.0f);
        const lua_Integer source =
            opts.get_or("source", static_cast<lua_Integer>(IREntity::kNullEntity));
        const std::int32_t ticks = opts.get_or("ticks", static_cast<std::int32_t>(-1));
        IRPrefab::Modifier::pushGlobal(
            field,
            kind,
            value,
            static_cast<IREntity::EntityId>(source),
            ticks
        );
    };

    api["addLambda"] = [](lua_Integer target,
                          sol::object fieldArg,
                          sol::protected_function fn,
                          sol::optional<sol::table> optsOpt) {
        const auto field = resolveFieldArg(fieldArg, "IRModifier.addLambda");
        lua_Integer source = static_cast<lua_Integer>(IREntity::kNullEntity);
        std::int32_t ticks = -1;
        if (optsOpt) {
            source = optsOpt->get_or("source", static_cast<lua_Integer>(IREntity::kNullEntity));
            ticks = optsOpt->get_or("ticks", static_cast<std::int32_t>(-1));
        }
        // `std::function<float(float)>` wraps the sol::protected_function
        // so the C_LambdaModifier vector can hold it as a typed callable.
        // The capture-by-value here is required: sol::protected_function
        // holds a Lua state reference plus a registry handle; copying it
        // bumps the registry refcount, which is the right semantics for
        // a modifier whose lifetime extends across frames.
        std::function<float(float)> wrapped = [fn](float in) -> float {
            sol::protected_function_result result = fn(in);
            if (!result.valid()) {
                sol::error err = result;
                IRE_LOG_ERROR("IRModifier.addLambda fn error: {}", err.what());
                return in;
            }
            return result.get<float>();
        };
        IRPrefab::Modifier::pushLambda(
            static_cast<IREntity::EntityId>(target),
            field,
            std::move(wrapped),
            static_cast<IREntity::EntityId>(source),
            ticks
        );
    };

    api["removeBySource"] = [](lua_Integer source) {
        IRPrefab::Modifier::removeBySource(static_cast<IREntity::EntityId>(source));
    };

    api["applyToField"] = [](lua_Integer target, sol::object fieldArg, float base) {
        const auto field = resolveFieldArg(fieldArg, "IRModifier.applyToField");
        return IRPrefab::Modifier::applyToField(
            static_cast<IREntity::EntityId>(target),
            field,
            base
        );
    };

    api["resolved"] =
        [](lua_Integer target, sol::object fieldArg, sol::optional<float> fallbackOpt) -> float {
        const auto field = resolveFieldArg(fieldArg, "IRModifier.resolved");
        const float fallback = fallbackOpt.value_or(0.0f);
        auto *resolved = IREntity::getComponentOptional<IRComponents::C_ResolvedFields>(
                             static_cast<IREntity::EntityId>(target)
        )
                             .value_or(nullptr);
        if (resolved == nullptr)
            return fallback;
        return resolved->get(field, fallback);
    };

    lua["IRModifier"] = api;
}

} // namespace IRScript::detail

#endif /* LUA_MODIFIER_BINDINGS_H */
