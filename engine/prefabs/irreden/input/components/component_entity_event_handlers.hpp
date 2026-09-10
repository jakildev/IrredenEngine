#ifndef COMPONENT_ENTITY_EVENT_HANDLERS_H
#define COMPONENT_ENTITY_EVENT_HANDLERS_H

#include <irreden/ir_profile.hpp>
#include <irreden/entity/ir_entity_types.hpp>

#include <sol/sol.hpp>
#include <vector>
#include <algorithm>

namespace IRComponents {

// World-scoped registry of Lua callbacks for entity hover/click events, held
// as a singleton component (`IREntity::singleton<C_EntityEventHandlers>()`)
// rather than a process static — the sanctioned pattern for world-scoped
// state per `.claude/rules/cpp-globals.md`.
//
// Lifetime is the reason this is a component (#2582). The vectors hold
// `sol::protected_function` refs into the World's Lua VM, so they must be
// destroyed while that VM is still open. `World` declares `m_lua` before the
// manager block (T-100 / #2446) precisely so archetype-column sol refs unref
// against a live `lua_State`, and `World::end()` runs `destroyAllEntities()`
// during `gameLoop()` while the VM is provably alive. Both orderings are
// structural, so nothing has to remember a teardown call — which is what
// retired #2572's manual `IREngine::gameLoop()` tail `clear()`. As a
// process-lifetime static this same state unref'd after `lua_close` at
// `__cxa_finalize` and segfaulted any creation that registered a handler.
//
// Singleton semantics: survives `resetGameplay()` (singleton entities are
// preserved and the cache is not cleared), dies at `destroyAllEntities()`.
// That matches the old static's scene-transition behaviour exactly.
struct C_EntityEventHandlers {
    struct HandlerEntry {
        int id_;
        sol::protected_function fn_;
    };

    std::vector<HandlerEntry> onHovered_;
    std::vector<HandlerEntry> onUnhovered_;
    std::vector<HandlerEntry> onClicked_;
    std::vector<HandlerEntry> onRightClick_;
    int nextId_ = 1;

    int addOnHovered(sol::protected_function fn) {
        int id = nextId_++;
        onHovered_.push_back({id, std::move(fn)});
        return id;
    }

    int addOnUnhovered(sol::protected_function fn) {
        int id = nextId_++;
        onUnhovered_.push_back({id, std::move(fn)});
        return id;
    }

    int addOnClicked(sol::protected_function fn) {
        int id = nextId_++;
        onClicked_.push_back({id, std::move(fn)});
        return id;
    }

    int addOnRightClick(sol::protected_function fn) {
        int id = nextId_++;
        onRightClick_.push_back({id, std::move(fn)});
        return id;
    }

    // The single enumeration of the handler-category vectors — clear() and
    // removeHandler() drive through it, so a new category is one edit here,
    // not a silent miss in a hand-maintained copy.
    template <typename F> void forEachHandlerVector(F &&f) {
        f(onHovered_);
        f(onUnhovered_);
        f(onClicked_);
        f(onRightClick_);
    }

    void removeHandler(int handlerId) {
        forEachHandlerVector([handlerId](std::vector<HandlerEntry> &vec) {
            vec.erase(
                std::remove_if(
                    vec.begin(),
                    vec.end(),
                    [handlerId](const HandlerEntry &e) { return e.id_ == handlerId; }
                ),
                vec.end()
            );
        });
    }

    // Drops every registered handler, destroying the sol::protected_functions
    // they hold. No longer load-bearing for shutdown — World teardown ordering
    // owns that now — but kept as the explicit "unsubscribe everything" verb
    // for creations swapping scripts mid-session, and as the tests' known-empty
    // baseline. nextId_ is intentionally left as-is: ids never recycle within a
    // world, so there is no id-reuse hazard to guard.
    void clear() {
        forEachHandlerVector([](std::vector<HandlerEntry> &vec) { vec.clear(); });
    }

    void fireHovered(IREntity::EntityId entityId) {
        fireAll(onHovered_, "onEntityHovered", entityId);
    }

    void fireUnhovered(IREntity::EntityId entityId) {
        fireAll(onUnhovered_, "onEntityUnhovered", entityId);
    }

    void fireClicked(IREntity::EntityId entityId, int button) {
        fireAll(onClicked_, "onEntityClicked", entityId, button);
    }

    void fireRightClick() {
        fireAll(onRightClick_, "onRightClick");
    }

  private:
    // The single dispatch body behind all four fire* verbs — same
    // consolidation `forEachHandlerVector` applies to clear()/removeHandler(),
    // for the same reason: a fifth event category is one call site here, not a
    // fourth hand-copied for/valid()/log block to keep in sync. `handlerName`
    // is the Lua-facing spelling, so an error message names the callback the
    // creation registered rather than this component's method.
    //
    // `args` is passed to each handler as an lvalue, NOT std::forward'd: the
    // pack is reused once per registered handler, so forwarding would move
    // out of it on the first iteration and hand later handlers a moved-from
    // value.
    template <typename... Args>
    void fireAll(std::vector<HandlerEntry> &handlers, const char *handlerName, Args &&...args) {
        for (auto &entry : handlers) {
            auto result = entry.fn_(args...);
            if (!result.valid()) {
                sol::error err = result;
                IRE_LOG_ERROR("{} handler error: {}", handlerName, err.what());
            }
        }
    }
};

} // namespace IRComponents

#endif /* COMPONENT_ENTITY_EVENT_HANDLERS_H */
