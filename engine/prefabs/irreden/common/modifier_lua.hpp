#ifndef MODIFIER_LUA_H
#define MODIFIER_LUA_H

// Behavior-API Lua bindings for the modifier framework.
// Exposes IRPrefab::Modifier:: free functions as the ir.modifier.* Lua namespace.
// Component/type bindings live separately in component_modifiers_lua.hpp.
//
// Call bindModifierNamespace(luaScript) during creation init, e.g. from the
// creation's lua_bindings.cpp, after the sol2 state is open.
//
// Entity IDs cross the Lua/C++ boundary as raw integers (IREntity::EntityId,
// which is uint64). Scripts with LuaEntity objects from createEntity calls
// should pass the .entity field: ir.modifier.push(entity.entity, { ... }).

#include <irreden/common/modifier.hpp>
#include <irreden/script/lua_script.hpp>

#include <string>
#include <unordered_set>

namespace IRScript {

inline void bindModifierNamespace(LuaScript &luaScript) {
    auto &lua = luaScript.lua();

    if (!lua["ir"].valid()) {
        lua["ir"] = lua.create_table();
    }
    auto modTbl = lua.create_table();

    modTbl["ADD"]       = static_cast<int>(IRComponents::TransformKind::ADD);
    modTbl["MULTIPLY"]  = static_cast<int>(IRComponents::TransformKind::MULTIPLY);
    modTbl["SET"]       = static_cast<int>(IRComponents::TransformKind::SET);
    modTbl["CLAMP_MIN"] = static_cast<int>(IRComponents::TransformKind::CLAMP_MIN);
    modTbl["CLAMP_MAX"] = static_cast<int>(IRComponents::TransformKind::CLAMP_MAX);
    modTbl["OVERRIDE"]  = static_cast<int>(IRComponents::TransformKind::OVERRIDE);

    modTbl["registerField"] = [](const char *name) -> int {
        static std::unordered_set<std::string> s_luaNames;
        const char *interned = s_luaNames.emplace(name).first->c_str();
        return static_cast<int>(IRPrefab::Modifier::registerField(interned));
    };

    struct PushOpts {
        IRComponents::FieldBindingId field_;
        IRComponents::TransformKind  kind_;
        float                        param_;
        IREntity::EntityId           source_;
        std::int32_t                 ticks_;
    };
    auto parseOpts = [](sol::table t) -> PushOpts {
        return {
            static_cast<IRComponents::FieldBindingId>(t.get<int>("field")),
            static_cast<IRComponents::TransformKind>(
                t.get_or("kind", static_cast<int>(IRComponents::TransformKind::ADD))),
            t.get_or("param", 0.0f),
            t.get_or<IREntity::EntityId>("source", IREntity::kNullEntity),
            t.get_or("ticks", -1)
        };
    };

    modTbl["push"] = [parseOpts](IREntity::EntityId target, sol::table opts) {
        auto o = parseOpts(opts);
        IRPrefab::Modifier::push(target, o.field_, o.kind_, o.param_, o.source_, o.ticks_);
    };

    modTbl["pushGlobal"] = [parseOpts](sol::table opts) {
        auto o = parseOpts(opts);
        IRPrefab::Modifier::pushGlobal(o.field_, o.kind_, o.param_, o.source_, o.ticks_);
    };

    // fn: Lua function(base: float) -> float.
    // ticks is reserved for a future lambda-decay system; lambda modifiers
    // never auto-expire regardless of the value passed. Use removeBySource to clean up.
    modTbl["pushLambda"] = [](IREntity::EntityId target, sol::table opts) {
        auto field = static_cast<IRComponents::FieldBindingId>(opts.get<int>("field"));
        sol::function fn = opts["fn"];
        if (!fn.valid()) return;
        auto source = opts.get_or<IREntity::EntityId>("source", IREntity::kNullEntity);
        int32_t ticks = opts.get_or("ticks", -1);
        IRPrefab::Modifier::pushLambda(
            target, field,
            [fn](float base) -> float { return fn.call<float>(base); },
            source, ticks
        );
    };

    modTbl["removeBySource"] = [](IREntity::EntityId source) {
        IRPrefab::Modifier::removeBySource(source);
    };

    modTbl["applyToField"] = [](IREntity::EntityId target, int field, float base) -> float {
        return IRPrefab::Modifier::applyToField(
            target,
            static_cast<IRComponents::FieldBindingId>(field),
            base
        );
    };

    lua["ir"]["modifier"] = modTbl;
}

} // namespace IRScript

#endif /* MODIFIER_LUA_H */
