#ifndef LUA_COLLISION_BINDINGS_H
#define LUA_COLLISION_BINDINGS_H

// IRCollision Lua bindings (engine #1817) — register Lua handlers that fire on
// AABB overlap enter / exit between two collision-layer-tagged entities.
//
//   IRCollision.onOverlapEnter(layerA, layerB, function(entA, entB) ... end)
//   IRCollision.onOverlapExit (layerA, layerB, function(entA, entB) ... end)
//
// Handlers are keyed by the (unordered) layer pair and stored in
// SYSTEM_DISPATCH_LUA_OVERLAP's session-lifetime state (NOT in a component) —
// the binding reaches that system through the LuaScript's prefab-system-id map
// (`IRSystem.systemId`'s mechanism), so a creation must
// `registerPrefabSystem<IRSystem::SYSTEM_DISPATCH_LUA_OVERLAP>()` and put the
// system in its UPDATE pipeline before registering handlers.
//
// The dispatcher passes the entity on `layerA` as the callback's FIRST arg and
// the entity on `layerB` as the second (D4 arg-ordering), so a creation can
// dispatch on the source entity's kind without re-checking layers in Lua. No
// physics response — overlap notification only.
//
// `IRCollision.Layer.*` mirrors the engine's `CollisionLayerMask` enum as an
// integer table (cpp-lua-enums.md). Raw integer layer bits beyond the named
// engine layers are also accepted — the dispatcher keys on the value, not the
// name — so gameplay layers (player / enemy / projectile / pickup) can be
// plain bit constants a creation defines.

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_system.hpp>
#include <irreden/script/lua_script.hpp>

#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/systems/system_dispatch_lua_overlap.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace IRScript::detail {

// Resolve the live SYSTEM_DISPATCH_LUA_OVERLAP instance via the LuaScript's
// prefab-system-id map, raising a Lua-visible error that points at the missing
// registration call (mirrors `IRSystem.systemId`'s fail-fast diagnostic).
inline IRSystem::System<IRSystem::SYSTEM_DISPATCH_LUA_OVERLAP> *
resolveOverlapDispatch(const std::unordered_map<int, IRSystem::SystemId> *prefabSystemIds) {
    auto it = prefabSystemIds->find(static_cast<int>(IRSystem::SYSTEM_DISPATCH_LUA_OVERLAP));
    if (it == prefabSystemIds->end()) {
        throw sol::error{
            "IRCollision.onOverlap*: SYSTEM_DISPATCH_LUA_OVERLAP is not registered "
            "— the C++ creation must call "
            "LuaScript::registerPrefabSystem<IRSystem::SYSTEM_DISPATCH_LUA_OVERLAP>() "
            "and add it to the UPDATE pipeline before main.lua registers handlers"
        };
    }
    auto *system =
        IRSystem::getSystemParams<IRSystem::System<IRSystem::SYSTEM_DISPATCH_LUA_OVERLAP>>(
            it->second
        );
    if (system == nullptr) {
        throw sol::error{
            "IRCollision.onOverlap*: overlap dispatch system params are null"
        };
    }
    return system;
}

inline void bindCollisionEvents(LuaScript &script) {
    sol::state &lua = script.lua();
    // Pointer to the live map (not a snapshot): the closures resolve the
    // dispatch system at call time, by which point the creation has registered
    // it. The map and the sol::state both outlive these closures (owned by
    // LuaScript), so capturing the raw pointer is safe.
    const std::unordered_map<int, IRSystem::SystemId> *prefabSystemIds =
        script.prefabSystemIds();

    if (!lua["IRCollision"].valid()) {
        lua["IRCollision"] = lua.create_table();
    }
    sol::table collision = lua["IRCollision"];

    if (!collision["Layer"].valid()) {
        sol::table layer = lua.create_table();
#define IR_BIND_COLLISION_LAYER(name) \
    layer[#name] = static_cast<lua_Integer>(IRComponents::CollisionLayerMask::name)
        IR_BIND_COLLISION_LAYER(COLLISION_LAYER_DEFAULT);
        IR_BIND_COLLISION_LAYER(COLLISION_LAYER_NOTE_BLOCK);
        IR_BIND_COLLISION_LAYER(COLLISION_LAYER_NOTE_PLATFORM);
        IR_BIND_COLLISION_LAYER(COLLISION_LAYER_PARTICLE);
#undef IR_BIND_COLLISION_LAYER
        collision["Layer"] = layer;
    }

    collision["onOverlapEnter"] =
        [prefabSystemIds](lua_Integer layerA, lua_Integer layerB, sol::protected_function fn) {
            resolveOverlapDispatch(prefabSystemIds)
                ->registerEnterHandler(
                    static_cast<std::uint32_t>(layerA),
                    static_cast<std::uint32_t>(layerB),
                    std::move(fn)
                );
        };
    collision["onOverlapExit"] =
        [prefabSystemIds](lua_Integer layerA, lua_Integer layerB, sol::protected_function fn) {
            resolveOverlapDispatch(prefabSystemIds)
                ->registerExitHandler(
                    static_cast<std::uint32_t>(layerA),
                    static_cast<std::uint32_t>(layerB),
                    std::move(fn)
                );
        };
}

} // namespace IRScript::detail

#endif /* LUA_COLLISION_BINDINGS_H */
