#ifndef SYSTEM_DISPATCH_LUA_OVERLAP_H
#define SYSTEM_DISPATCH_LUA_OVERLAP_H

// DISPATCH_LUA_OVERLAP (#1817) — turns the batched overlap pairs that
// COLLISION_NOTE_PLATFORM emits into Lua-facing enter/exit callbacks.
//
// Handlers are registered by collision-LAYER PAIR (not per-entity): a creation
// calls `IRCollision.onOverlapEnter(layerA, layerB, fn)` and the binding stores
// the `sol::protected_function` here (see `lua_collision_bindings.hpp`). Keying
// by layer pair, held in this system's session-lifetime state, sidesteps the
// per-entity sol-handle lifetime problem entirely — handlers outlive any
// individual entity, and a dead entity simply produces no further contacts.
//
// The `sol::protected_function`s held here are freed when the SystemManager is
// destroyed, which `World` orders BEFORE the `sol::state` (`m_lua` leads the
// manager block in `world.hpp`) — so the refs into Lua are always released
// while the state is still open. Same lifetime contract the CommandManager
// relies on (`lua_command_bindings.hpp`).
//
// Enter/exit is derived at PAIR granularity (D3): an entity touching several
// others is tracked correctly, unlike the per-entity single-contact
// `C_ContactEvent` (which is left untouched for its existing consumers).

#include <irreden/ir_entity.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/update/components/component_overlap_contact_batch.hpp>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <cstdint>
#include <unordered_map>
#include <vector>

using namespace IRComponents;

namespace IRSystem {

namespace detail {

// Order-independent 64-bit key for a collision-layer pair. A handler
// registered for (A, B) matches a contact pair whose entities carry layers
// {A, B} regardless of which entity is `a_`.
inline std::uint64_t overlapLayerPairKey(std::uint32_t layerA, std::uint32_t layerB) {
    const std::uint32_t lo = layerA < layerB ? layerA : layerB;
    const std::uint32_t hi = layerA < layerB ? layerB : layerA;
    return (static_cast<std::uint64_t>(lo) << 32) | static_cast<std::uint64_t>(hi);
}

// Exact key for an entity pair. `EntityId` is 64-bit so two ids can't pack
// into one word — use both, with a hash-combine. The pair is always stored
// canonically (a_ < b_) by the producer, so {a_, b_} is unique per pair.
struct OverlapEntityPairKey {
    IREntity::EntityId a_;
    IREntity::EntityId b_;
    bool operator==(const OverlapEntityPairKey &other) const {
        return a_ == other.a_ && b_ == other.b_;
    }
};
struct OverlapEntityPairHash {
    std::size_t operator()(const OverlapEntityPairKey &key) const {
        std::size_t h = std::hash<IREntity::EntityId>{}(key.a_);
        h ^= std::hash<IREntity::EntityId>{}(key.b_) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

} // namespace detail

template <> struct System<DISPATCH_LUA_OVERLAP> {
    // One registered Lua handler. `layer1_` / `layer2_` are the layers as the
    // author ordered them in `onOverlapEnter(layer1, layer2, fn)`; the
    // dispatcher passes the entity on `layer1_` as the callback's first arg.
    struct Handler {
        std::uint32_t layer1_;
        std::uint32_t layer2_;
        sol::protected_function fn_;
    };

    // Layer-pair key → handlers, one map per edge of the contact lifecycle.
    std::unordered_map<std::uint64_t, std::vector<Handler>> enterHandlers_;
    std::unordered_map<std::uint64_t, std::vector<Handler>> exitHandlers_;

    // Pair-level enter/exit state (D3). The value keeps the full ContactPair
    // so an EXIT — whose pair is absent from the current frame — still has the
    // layers it needs to route to the right exit handler. Both maps reuse
    // their bucket capacity across frames (clear + swap, never reallocate).
    using PairMap = std::unordered_map<
        detail::OverlapEntityPairKey,
        ContactPair,
        detail::OverlapEntityPairHash>;
    PairMap previousPairs_;
    PairMap currentPairs_;

    void registerEnterHandler(std::uint32_t layer1, std::uint32_t layer2, sol::protected_function fn) {
        addHandler(enterHandlers_, layer1, layer2, std::move(fn));
    }
    void registerExitHandler(std::uint32_t layer1, std::uint32_t layer2, sol::protected_function fn) {
        addHandler(exitHandlers_, layer1, layer2, std::move(fn));
    }

    void tick(C_OverlapContactBatch &batch) {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_UPDATE);

        currentPairs_.clear();
        for (const ContactPair &pair : batch.pairs_) {
            currentPairs_.emplace(detail::OverlapEntityPairKey{pair.a_, pair.b_}, pair);
        }

        // ENTER — present this frame, absent last frame.
        for (const auto &[key, pair] : currentPairs_) {
            if (previousPairs_.find(key) == previousPairs_.end()) {
                invokeHandlers(enterHandlers_, pair);
            }
        }
        // EXIT — present last frame, absent this frame.
        for (const auto &[key, pair] : previousPairs_) {
            if (currentPairs_.find(key) == currentPairs_.end()) {
                invokeHandlers(exitHandlers_, pair);
            }
        }

        previousPairs_.swap(currentPairs_);
        // Consume the batch: the producer refills from empty next frame. Owning
        // the clear here (rather than in COLLISION_EVENT_CLEAR) keeps
        // produce → consume → clear local to the two new systems and leaves the
        // existing per-entity clear untouched.
        batch.pairs_.clear();
    }

    static SystemId create() {
        // Creating the singleton IS the producer's opt-in: COLLISION_NOTE_PLATFORM
        // emits pairs only when `C_OverlapContactBatch` exists, so registering
        // this system switches emission on. Done before the first tick so the
        // producer (which runs earlier in the pipeline) sees it on frame 1.
        IREntity::singletonEntity<C_OverlapContactBatch>();
        return registerSystem<DISPATCH_LUA_OVERLAP, C_OverlapContactBatch>(
            "DispatchLuaOverlap"
        );
    }

  private:
    static void addHandler(
        std::unordered_map<std::uint64_t, std::vector<Handler>> &handlers,
        std::uint32_t layer1,
        std::uint32_t layer2,
        sol::protected_function fn
    ) {
        handlers[detail::overlapLayerPairKey(layer1, layer2)].push_back(
            Handler{layer1, layer2, std::move(fn)}
        );
    }

    static void invokeHandlers(
        const std::unordered_map<std::uint64_t, std::vector<Handler>> &handlers,
        const ContactPair &pair
    ) {
        auto it = handlers.find(detail::overlapLayerPairKey(pair.layerA_, pair.layerB_));
        if (it == handlers.end()) {
            return;
        }
        for (const Handler &handler : it->second) {
            // Pass the entity on the handler's `layer1_` first (D4). When the
            // pair's `a_` is on `layer1_` the canonical order already matches;
            // otherwise `a_` is on `layer2_`, so swap. Same-layer handlers
            // (layer1_ == layer2_) take the canonical (a_, b_) branch.
            IREntity::EntityId first = pair.a_;
            IREntity::EntityId second = pair.b_;
            if (handler.layer1_ != pair.layerA_) {
                first = pair.b_;
                second = pair.a_;
            }
            sol::protected_function_result result =
                handler.fn_(static_cast<lua_Integer>(first), static_cast<lua_Integer>(second));
            if (!result.valid()) {
                sol::error err = result;
                IRE_LOG_ERROR("Lua overlap handler error: {}", err.what());
            }
        }
    }
};

} // namespace IRSystem

#endif /* SYSTEM_DISPATCH_LUA_OVERLAP_H */
