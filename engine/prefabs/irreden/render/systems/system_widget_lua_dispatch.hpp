#ifndef SYSTEM_WIDGET_LUA_DISPATCH_H
#define SYSTEM_WIDGET_LUA_DISPATCH_H

// WIDGET_LUA_DISPATCH (#1975) — turns a widget's per-frame click pulse
// (C_WidgetState::fireAction_, set by WIDGET_INPUT on click-release-over-
// widget) into a Lua `onClick` callback. The one piece of net-new
// infrastructure the widget framework lacked: click was poll-only
// (IRPrefab::Widget::wasClicked) until now.
//
// Handlers are registered per WIDGET ENTITY — a creation (typically from
// Lua) calls `IRGui.makeButton(..., onClick)` and the binding stores the
// `sol::protected_function` here keyed by the button's EntityId (see
// `lua_widget_bindings.hpp`). The binding reaches this system through the
// LuaScript's prefab-system-id map, so a creation must
// `registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>()` and place it in
// its INPUT pipeline immediately AFTER `WIDGET_INPUT` (so `fireAction_` is
// fresh) and before the per-kind render/clear systems.
//
// The `sol::protected_function`s held here are freed when the SystemManager
// is destroyed, which `World` orders BEFORE the `sol::state` (`m_lua` leads
// the manager block in `world.hpp`) — so the refs into Lua are always
// released while the state is still open. Same lifetime contract
// DISPATCH_LUA_OVERLAP (#1817) and the CommandManager rely on.
//
// A destroyed widget leaves a stale `clickHandlers_` entry (bounded leak;
// the handler simply never fires again, since the dead entity stops
// producing `fireAction_` pulses — same benign behavior as
// DISPATCH_LUA_OVERLAP's per-pair handlers). Acceptable for v1; no
// per-frame scan to reap entries.
//
// SERIAL by design: the handler invoke calls into LuaJIT via sol2, which is
// single-threaded — do NOT mark PARALLEL_FOR (the same constraint
// DISPATCH_LUA_OVERLAP relies on).

#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_widget.hpp>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <unordered_map>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<WIDGET_LUA_DISPATCH> {
    // Widget EntityId → its Lua click handler. Lives on the System (not a
    // function-local static — see .claude/rules/cpp-systems.md), so it is
    // owned by the system entity's params slot and freed with it.
    std::unordered_map<IREntity::EntityId, sol::protected_function> clickHandlers_;

    void registerClickHandler(IREntity::EntityId widget, sol::protected_function fn) {
        clickHandlers_[widget] = std::move(fn);
    }

    // Per-entity-id tick over C_WidgetState. The map lookup is keyed by the
    // iterating entity's own id (not a getComponent on a foreign entity), so
    // it is allowed inside a tick. Non-interactive widgets (panels) also
    // carry C_WidgetState but never pulse fireAction_, and unregistered
    // widgets simply miss the map — both no-ops.
    void tick(IREntity::EntityId id, C_WidgetState &state) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_INPUT);

        if (!state.fireAction_) {
            return;
        }
        auto it = clickHandlers_.find(id);
        if (it == clickHandlers_.end()) {
            return;
        }
        sol::protected_function_result result = it->second(static_cast<lua_Integer>(id));
        if (!result.valid()) {
            sol::error err = result;
            IRE_LOG_ERROR("Lua widget onClick handler error: {}", err.what());
        }
    }

    static SystemId create() {
        return registerSystem<WIDGET_LUA_DISPATCH, C_WidgetState>("WidgetLuaDispatch");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_LUA_DISPATCH_H */
