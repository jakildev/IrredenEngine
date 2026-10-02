#ifndef COMPONENT_ENTITY_EVENT_HANDLERS_H
#define COMPONENT_ENTITY_EVENT_HANDLERS_H

#include <irreden/ir_profile.hpp>
#include <irreden/entity/ir_entity_types.hpp>

#include <sol/sol.hpp>
#include <cstddef>
#include <vector>

namespace IRComponents {

// The `button` argument `fireClicked` hands an `onEntityClicked` handler.
// Mirrored to Lua as `IRInput.MouseButton` — a click-dispatch code, not an
// `IRInput.Key` value (`IRInput.Key.MOUSE_LEFT` is a different number).
enum class EntityClickButton : int { LEFT = 0, RIGHT = 1 };

// World-scoped registry of Lua callbacks for entity hover/click events, held
// as a singleton component (`IREntity::singleton<C_EntityEventHandlers>()`)
// rather than a process static — the sanctioned pattern for world-scoped
// state per `.claude/rules/cpp-globals.md`.
//
// The vectors hold `sol::protected_function` refs into the World's Lua VM,
// so they must be destroyed while that VM is still open — that dependency
// is why this state is a component, not a process static. `World` declares
// `m_lua` before the manager block precisely so archetype-column sol refs
// unref against a live `lua_State`, and `World::end()` runs
// `destroyAllEntities()` during `gameLoop()` while the VM is provably
// alive. Both orderings are structural, so nothing has to remember a
// teardown call.
//
// Singleton semantics: survives `resetGameplay()` (singleton entities are
// preserved and the cache is not cleared), dies at `destroyAllEntities()`.
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

    // Safe to call from inside a handler: mid-dispatch, the entry is
    // tombstoned (skipped for the rest of the pass) and erased once the
    // outermost pass ends.
    void removeHandler(int handlerId) {
        forEachHandlerVector([handlerId](std::vector<HandlerEntry> &vec) {
            for (auto &entry : vec) {
                if (entry.id_ == handlerId) {
                    entry.id_ = kRemovedId;
                }
            }
        });
        compactUnlessDispatching();
    }

    // Drops every registered handler, destroying the sol::protected_functions
    // they hold. Shutdown does not depend on it (World teardown ordering owns
    // that); it is the explicit "unsubscribe everything" verb for creations
    // swapping scripts mid-session.
    // nextId_ is left as-is: ids never recycle within a world, so there is no
    // id-reuse hazard to guard.
    // Mid-dispatch it tombstones like removeHandler().
    void clear() {
        forEachHandlerVector([](std::vector<HandlerEntry> &vec) {
            for (auto &entry : vec) {
                entry.id_ = kRemovedId;
            }
        });
        compactUnlessDispatching();
    }

    void fireHovered(IREntity::EntityId entityId) {
        fireAll(onHovered_, "onEntityHovered", entityId);
    }

    void fireUnhovered(IREntity::EntityId entityId) {
        fireAll(onUnhovered_, "onEntityUnhovered", entityId);
    }

    void fireClicked(IREntity::EntityId entityId, EntityClickButton button) {
        fireAll(onClicked_, "onEntityClicked", entityId, static_cast<int>(button));
    }

    void fireRightClick() {
        fireAll(onRightClick_, "onRightClick");
    }

  private:
    // Ids start at 1, so 0 never names a live handler.
    static constexpr int kRemovedId = 0;
    int dispatchDepth_ = 0;

    void compactUnlessDispatching() {
        if (dispatchDepth_ > 0) {
            return;
        }
        forEachHandlerVector([](std::vector<HandlerEntry> &vec) {
            std::erase_if(vec, [](const HandlerEntry &e) { return e.id_ == kRemovedId; });
        });
    }

    // `handlerName` is the Lua-facing spelling, so an error message names the
    // callback the creation registered rather than this component's method.
    //
    // A handler may register or remove handlers (`IRInput.*` from Lua). The
    // pass walks by index up to the size it started with, so an append, even
    // one that reallocates, neither invalidates the walk nor fires before the
    // next pass. Removals are tombstones until the outermost pass compacts.
    // Each call goes through a copy of the function, so a handler removing
    // itself does not destroy the ref it is running from.
    //
    // `args` is passed to each handler as an lvalue, NOT std::forward'd: the
    // pack is reused once per registered handler, so forwarding would move
    // out of it on the first iteration and hand later handlers a moved-from
    // value.
    template <typename... Args>
    void fireAll(std::vector<HandlerEntry> &handlers, const char *handlerName, Args &&...args) {
        ++dispatchDepth_;
        const std::size_t count = handlers.size();
        for (std::size_t i = 0; i < count; ++i) {
            if (handlers[i].id_ == kRemovedId) {
                continue;
            }
            sol::protected_function fn = handlers[i].fn_;
            auto result = fn(args...);
            if (!result.valid()) {
                sol::error err = result;
                IRE_LOG_ERROR("{} handler error: {}", handlerName, err.what());
            }
        }
        --dispatchDepth_;
        compactUnlessDispatching();
    }
};

} // namespace IRComponents

#endif /* COMPONENT_ENTITY_EVENT_HANDLERS_H */
