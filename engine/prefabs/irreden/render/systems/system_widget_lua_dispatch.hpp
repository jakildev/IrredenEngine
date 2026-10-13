#ifndef SYSTEM_WIDGET_LUA_DISPATCH_H
#define SYSTEM_WIDGET_LUA_DISPATCH_H

// WIDGET_LUA_DISPATCH — turns a widget's per-frame interaction state, as
// WIDGET_INPUT leaves it in C_WidgetState, into Lua callbacks:
//   - click: `fireAction_` (click released over the widget) → onClick(id)
//   - hover: a rise / fall of `hovered_` → onHover(id) / onUnhover(id)
//
// Handlers are registered per WIDGET ENTITY, typically from Lua through
// `IRGui.makeButton(..., onClick)` / `IRGui.makeHitRegion(..., opts)`
// (`lua_widget_bindings.hpp`). The binding reaches this system through the
// LuaScript's prefab-system-id map, so a creation must
// `registerPrefabSystem<IRSystem::WIDGET_LUA_DISPATCH>()` and place it in its
// INPUT pipeline immediately AFTER `WIDGET_INPUT`, so the state is fresh.
//
// `hovered_` is WIDGET_INPUT's z-order-routed hover, so at most one widget is
// hovered at a time and an overlapped widget never sees a hover.
//
// The tick only records events; `endTick` fires them once every widget has
// been visited, all unhovers first, then hovers, then clicks. A handler
// therefore runs outside archetype iteration and may build or destroy
// widgets, and moving the cursor from one widget to another always reports
// the unhover before the hover.
//
// A destroyed widget's handlers are dropped by a pre-destroy hook, so a
// recycled entity id never inherits them. No unhover is delivered for a
// widget destroyed while hovered.
//
// The `sol::protected_function`s held here are freed when the SystemManager
// is destroyed, which `World` orders BEFORE the `sol::state` (`m_lua` leads
// the manager block in `world.hpp`) — so the refs into Lua are always
// released while the state is still open.
//
// SERIAL by design: handlers call into LuaJIT via sol2, which is
// single-threaded — do NOT mark PARALLEL_FOR.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>

#include <irreden/render/components/component_widget.hpp>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <unordered_map>
#include <vector>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<WIDGET_LUA_DISPATCH> {
    // Either function may be empty (an invalid protected_function), in which
    // case that edge is tracked but not reported.
    struct HoverHandlers {
        sol::protected_function onHover_;
        sol::protected_function onUnhover_;
        bool hovered_ = false;
    };

    // Firing order within one frame is the enumerator order.
    enum class WidgetEvent : int { UNHOVER = 0, HOVER = 1, CLICK = 2 };

    struct PendingEvent {
        IREntity::EntityId widget_;
        WidgetEvent event_;
    };

    std::unordered_map<IREntity::EntityId, sol::protected_function> clickHandlers_;
    std::unordered_map<IREntity::EntityId, HoverHandlers> hoverHandlers_;
    // A widget records at most one hover edge and one click per frame, so
    // beginTick's reserve bounds it and the tick never reallocates.
    std::vector<PendingEvent> pending_;

    void registerClickHandler(IREntity::EntityId widget, sol::protected_function fn) {
        clickHandlers_[widget] = std::move(fn);
    }

    // The first report is the widget's next hover; a widget registered while
    // already hovered reports that hover on the next tick.
    void registerHoverHandlers(
        IREntity::EntityId widget,
        sol::protected_function onHover,
        sol::protected_function onUnhover
    ) {
        hoverHandlers_[widget] = HoverHandlers{std::move(onHover), std::move(onUnhover), false};
    }

    void forgetWidget(IREntity::EntityId widget) {
        clickHandlers_.erase(widget);
        hoverHandlers_.erase(widget);
    }

    void beginTick() {
        pending_.reserve(hoverHandlers_.size() + clickHandlers_.size());
    }

    // The map lookups are keyed by the iterating entity's own id (not a
    // getComponent on a foreign entity), so they are allowed inside a tick.
    // Widgets with no registered handler miss both maps — a no-op.
    void tick(IREntity::EntityId id, const C_WidgetState &state) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_INPUT);

        if (!hoverHandlers_.empty()) {
            auto hover = hoverHandlers_.find(id);
            if (hover != hoverHandlers_.end() && hover->second.hovered_ != state.hovered_) {
                hover->second.hovered_ = state.hovered_;
                pending_.push_back(
                    {id, state.hovered_ ? WidgetEvent::HOVER : WidgetEvent::UNHOVER}
                );
            }
        }
        if (state.fireAction_ && clickHandlers_.contains(id)) {
            pending_.push_back({id, WidgetEvent::CLICK});
        }
    }

    void endTick() {
        for (WidgetEvent pass : {WidgetEvent::UNHOVER, WidgetEvent::HOVER, WidgetEvent::CLICK}) {
            for (const PendingEvent &pending : pending_) {
                if (pending.event_ == pass) {
                    fire(pending);
                }
            }
        }
        pending_.clear();
    }

    static SystemId create() {
        SystemId id = registerSystem<WIDGET_LUA_DISPATCH, C_WidgetState>("WidgetLuaDispatch");
        auto *params = getSystemParams<System<WIDGET_LUA_DISPATCH>>(id);
        IREntity::getEntityManager().registerPreDestroyHook([params](IREntity::EntityId destroyed) {
            params->forgetWidget(destroyed);
        });
        return id;
    }

  private:
    // Resolves the handler at fire time, so a handler an earlier callback
    // replaced or dropped this frame is honoured. The call goes through a
    // copy, so a callback that re-registers its own widget does not destroy
    // the function it is running from.
    void fire(const PendingEvent &pending) {
        sol::protected_function fn;
        if (pending.event_ == WidgetEvent::CLICK) {
            auto it = clickHandlers_.find(pending.widget_);
            if (it == clickHandlers_.end()) {
                return;
            }
            fn = it->second;
        } else {
            auto it = hoverHandlers_.find(pending.widget_);
            if (it == hoverHandlers_.end()) {
                return;
            }
            fn = pending.event_ == WidgetEvent::HOVER ? it->second.onHover_ : it->second.onUnhover_;
        }
        if (!fn.valid()) {
            return;
        }
        sol::protected_function_result result = fn(static_cast<lua_Integer>(pending.widget_));
        if (!result.valid()) {
            sol::error err = result;
            IRE_LOG_ERROR("Lua widget handler error: {}", err.what());
        }
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_LUA_DISPATCH_H */
