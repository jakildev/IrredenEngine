#ifndef LUA_COLLISION_BINDINGS_H
#define LUA_COLLISION_BINDINGS_H

// IRCollision Lua bindings (engine #1817).
//
// Surfaces the engine's AABB overlap detection to Lua as overlap EVENTS: a
// creation registers a Lua handler keyed by a collision-layer pair, and the
// handler fires on enter (and exit) of any two entities on those layers,
// receiving both entity ids. No physics solver — overlap notification only.
//
// The handlers live on the COLLISION_DISPATCH_LUA_OVERLAP system's state
// (layer-pair-keyed registry); the producer COLLISION_NOTE_PLATFORM emits the
// batched ContactPair vector the dispatcher diffs each frame. This binding is
// the thin Lua front: it wraps the `sol::protected_function` into a plain
// `std::function` (so the prefab system stays sol-free) with the same
// error-trapping idiom `lua_command_bindings` uses, then hands it to the
// dispatch system reached via the per-LuaScript prefab-system id map.

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/script/lua_script.hpp>

#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/systems/system_dispatch_lua_overlap.hpp>

#include <cstdint>
#include <unordered_map>

namespace IRScript::detail {

// Resolve the live COLLISION_DISPATCH_LUA_OVERLAP system instance for this
// LuaScript. Deferred to call time (not bind time): the closure captures the
// prefab-system id map pointer, and the creation registers the dispatch
// system after `bindLuaDrivenEcs()` runs (`registerPrefabSystem<...>()`), so
// the id only exists once Lua actually calls `onOverlapEnter`. Raises a
// Lua-visible error when the dispatch system was never registered — same
// fail-fast contract as `IRSystem.systemId`.
inline IRSystem::System<IRSystem::COLLISION_DISPATCH_LUA_OVERLAP> *
reachOverlapDispatchSystem(const std::unordered_map<int, IRSystem::SystemId> *prefabSystemIds) {
    auto it = prefabSystemIds->find(
        static_cast<int>(IRSystem::COLLISION_DISPATCH_LUA_OVERLAP)
    );
    if (it == prefabSystemIds->end()) {
        throw sol::error{
            "IRCollision.onOverlap*: the creation must call "
            "registerPrefabSystem<COLLISION_DISPATCH_LUA_OVERLAP>() (and place "
            "the system in its UPDATE pipeline after COLLISION_NOTE_PLATFORM) "
            "before registering Lua overlap handlers"
        };
    }
    auto *system = IRSystem::getSystemParams<
        IRSystem::System<IRSystem::COLLISION_DISPATCH_LUA_OVERLAP>>(it->second);
    if (system == nullptr) {
        throw sol::error{"IRCollision.onOverlap*: dispatch system params missing"};
    }
    return system;
}

// Wrap a Lua handler into the sol-free callback the dispatch system stores.
// The two entity ids cross to Lua as plain numbers; errors raised inside the
// handler are trapped + logged (the dispatch loop continues) — mirrors
// `lua_command_bindings`' command-body wrapper.
inline IRSystem::System<IRSystem::COLLISION_DISPATCH_LUA_OVERLAP>::OverlapCallback
wrapOverlapHandler(sol::protected_function fn, const char *bindingName) {
    return [fn = std::move(fn), bindingName](IREntity::EntityId a, IREntity::EntityId b) {
        sol::protected_function_result result = fn(a, b);
        if (!result.valid()) {
            sol::error err = result;
            IRE_LOG_ERROR("{} handler error: {}", bindingName, err.what());
        }
    };
}

// Bind `IRCollision.Layer` (the built-in collision-layer enum as an integer
// table) + `IRCollision.onOverlapEnter` / `onOverlapExit`. Lua may also pass
// raw integer layer bits, so creations with their own (arcade) layer
// vocabulary aren't limited to the four named layers. Idempotent.
inline void bindCollisionEvents(
    LuaScript &script, const std::unordered_map<int, IRSystem::SystemId> *prefabSystemIds
) {
    sol::state &lua = script.lua();
    if (!lua["IRCollision"].valid()) {
        lua["IRCollision"] = lua.create_table();
    }
    if (lua["IRCollision"]["onOverlapEnter"].valid()) {
        return; // idempotent
    }

    // `Layer` table — keys derive from the CollisionLayerMask enum via the
    // shared `COLLISION_LAYER_` prefix, so the value can't drift from the C++
    // enum (a renamed enum value breaks the build). Spell layers via
    // `IRCollision.Layer.X` in Lua, never a bare string.
    sol::table layers = lua.create_table();
#define IR_BIND_LAYER(shortName) \
    layers[#shortName] = static_cast<lua_Integer>(IRComponents::COLLISION_LAYER_##shortName)
    IR_BIND_LAYER(DEFAULT);
    IR_BIND_LAYER(NOTE_BLOCK);
    IR_BIND_LAYER(NOTE_PLATFORM);
    IR_BIND_LAYER(PARTICLE);
#undef IR_BIND_LAYER
    lua["IRCollision"]["Layer"] = layers;

    // onOverlapEnter(layerA, layerB, fn) — fn(entityOnLayerA, entityOnLayerB)
    // fires the frame the two entities begin overlapping. For a same-layer
    // pair (layerA == layerB) the order is the canonical (smaller id, larger
    // id).
    lua["IRCollision"]["onOverlapEnter"] =
        [prefabSystemIds](lua_Integer layerA, lua_Integer layerB, sol::protected_function fn) {
            auto *system = reachOverlapDispatchSystem(prefabSystemIds);
            system->registerEnter(
                static_cast<std::uint32_t>(layerA),
                static_cast<std::uint32_t>(layerB),
                wrapOverlapHandler(std::move(fn), "IRCollision.onOverlapEnter")
            );
        };

    // onOverlapExit(layerA, layerB, fn) — fires the frame the overlap ends.
    // The ids may reference a despawned entity (an overlap ends when an entity
    // dies); guard with IREntity.exists before touching them.
    lua["IRCollision"]["onOverlapExit"] =
        [prefabSystemIds](lua_Integer layerA, lua_Integer layerB, sol::protected_function fn) {
            auto *system = reachOverlapDispatchSystem(prefabSystemIds);
            system->registerExit(
                static_cast<std::uint32_t>(layerA),
                static_cast<std::uint32_t>(layerB),
                wrapOverlapHandler(std::move(fn), "IRCollision.onOverlapExit")
            );
        };
}

} // namespace IRScript::detail

#endif /* LUA_COLLISION_BINDINGS_H */
