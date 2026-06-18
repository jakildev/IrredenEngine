#ifndef SYSTEM_DISPATCH_LUA_OVERLAP_H
#define SYSTEM_DISPATCH_LUA_OVERLAP_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/update/components/component_collision_pair_buffer.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace IRSystem {

// Drives Lua-facing overlap callbacks from the batched ContactPair vector
// COLLISION_NOTE_PLATFORM fills each frame (#1817). It owns the pair-level
// enter/exit state machine and the layer-pair-keyed handler registry.
//
// Layering note: handlers are stored as plain
// `std::function<void(EntityId, EntityId)>`, NOT `sol::protected_function`,
// so this prefab header has no sol2 dependency. The Lua binding
// (`engine/script/.../lua_collision_bindings.hpp`) wraps the
// `sol::protected_function` (with error trapping) into the std::function
// before handing it here — mirroring how `lua_command_bindings` stores a
// std::function in the CommandManager.
//
// Pipeline placement: register AFTER COLLISION_NOTE_PLATFORM (so the pair
// buffer is populated) and BEFORE the producer's next-frame beginTick clears
// it — i.e. anywhere after NOTE_PLATFORM in the same UPDATE pass.
template <> struct System<COLLISION_DISPATCH_LUA_OVERLAP> {
    using OverlapCallback = std::function<void(IREntity::EntityId, IREntity::EntityId)>;

    // One registered Lua handler. `layerFirst_` / `layerSecond_` preserve the
    // ARGUMENT order the caller registered (`onOverlapEnter(layerFirst,
    // layerSecond, fn)`), so the dispatcher can order the two entity ids to
    // match — `fn(entityOnLayerFirst, entityOnLayerSecond)`.
    struct HandlerEntry {
        std::uint32_t layerFirst_;
        std::uint32_t layerSecond_;
        OverlapCallback fn_;
    };

    // Handlers keyed by the canonical (unordered) layer pair. Both directions
    // of a cross-layer pair land under the same key; the per-entry
    // layerFirst_/layerSecond_ recovers the registered arg order. Populated at
    // setup (handler registration), read each frame — not a per-tick alloc.
    std::unordered_map<std::uint64_t, std::vector<HandlerEntry>> enterHandlers_;
    std::unordered_map<std::uint64_t, std::vector<HandlerEntry>> exitHandlers_;

    // Last-frame and scratch pair sets, each kept SORTED by (a_, b_) so the
    // frame-to-frame diff is a linear merge. Members (not locals) with
    // clear-not-release + swap, so the steady state allocates nothing.
    std::vector<IRComponents::ContactPair> prevPairs_;
    std::vector<IRComponents::ContactPair> currentPairs_;

    static std::uint64_t layerKey(std::uint32_t layerA, std::uint32_t layerB) {
        const std::uint32_t lo = layerA < layerB ? layerA : layerB;
        const std::uint32_t hi = layerA < layerB ? layerB : layerA;
        return (static_cast<std::uint64_t>(lo) << 32) | static_cast<std::uint64_t>(hi);
    }

    // Order by the canonical (a_, b_) entity-id pair. The producer already
    // emits a_ < b_, so the entity ids alone are a total order over pairs.
    static bool pairLess(const IRComponents::ContactPair &x, const IRComponents::ContactPair &y) {
        if (x.a_ != y.a_) {
            return x.a_ < y.a_;
        }
        return x.b_ < y.b_;
    }
    static int pairCompare(const IRComponents::ContactPair &x, const IRComponents::ContactPair &y) {
        if (x.a_ != y.a_) {
            return x.a_ < y.a_ ? -1 : 1;
        }
        if (x.b_ != y.b_) {
            return x.b_ < y.b_ ? -1 : 1;
        }
        return 0;
    }

    void registerEnter(std::uint32_t layerA, std::uint32_t layerB, OverlapCallback fn) {
        enterHandlers_[layerKey(layerA, layerB)].push_back(
            HandlerEntry{layerA, layerB, std::move(fn)}
        );
    }

    void registerExit(std::uint32_t layerA, std::uint32_t layerB, OverlapCallback fn) {
        exitHandlers_[layerKey(layerA, layerB)].push_back(
            HandlerEntry{layerA, layerB, std::move(fn)}
        );
    }

    void dispatch(
        const std::unordered_map<std::uint64_t, std::vector<HandlerEntry>> &handlers,
        const IRComponents::ContactPair &pair
    ) {
        auto it = handlers.find(layerKey(pair.layerA_, pair.layerB_));
        if (it == handlers.end()) {
            return;
        }
        for (const HandlerEntry &handler : it->second) {
            IREntity::EntityId arg1 = pair.a_;
            IREntity::EntityId arg2 = pair.b_;
            // Cross-layer: if a_ sits on the handler's SECOND layer, swap so
            // the callback always receives (entityOnFirst, entityOnSecond).
            // Same-layer handlers (first == second) never swap — the canonical
            // (a_, b_) order is the documented contract.
            if (handler.layerFirst_ != handler.layerSecond_ &&
                pair.layerA_ == handler.layerSecond_) {
                arg1 = pair.b_;
                arg2 = pair.a_;
            }
            handler.fn_(arg1, arg2);
        }
    }

    void tick(IRComponents::C_CollisionPairBuffer &buffer) {
        currentPairs_.clear();
        currentPairs_.insert(currentPairs_.end(), buffer.pairs_.begin(), buffer.pairs_.end());
        std::sort(currentPairs_.begin(), currentPairs_.end(), pairLess);

        // Linear merge of the two sorted sets. Each producer emits a pair once,
        // so neither set has duplicate keys.
        std::size_t i = 0;
        std::size_t j = 0;
        while (i < prevPairs_.size() && j < currentPairs_.size()) {
            const int cmp = pairCompare(prevPairs_[i], currentPairs_[j]);
            if (cmp == 0) {
                ++i; // present both frames — sustained overlap, no event
                ++j;
            } else if (cmp < 0) {
                dispatch(exitHandlers_, prevPairs_[i]); // gone this frame -> EXIT
                ++i;
            } else {
                dispatch(enterHandlers_, currentPairs_[j]); // new this frame -> ENTER
                ++j;
            }
        }
        // EXIT for any pairs that despawned: the ids may now reference a dead
        // entity (an overlap ends when an entity dies) — Lua exit handlers
        // should guard with IREntity.exists before touching them.
        for (; i < prevPairs_.size(); ++i) {
            dispatch(exitHandlers_, prevPairs_[i]);
        }
        for (; j < currentPairs_.size(); ++j) {
            dispatch(enterHandlers_, currentPairs_[j]);
        }

        std::swap(prevPairs_, currentPairs_);
    }

    static SystemId create() {
        return registerSystem<
            COLLISION_DISPATCH_LUA_OVERLAP,
            IRComponents::C_CollisionPairBuffer>("CollisionDispatchLuaOverlap");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_DISPATCH_LUA_OVERLAP_H */
