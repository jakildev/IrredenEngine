#ifndef SYSTEM_MODIFIER_DECAY_H
#define SYSTEM_MODIFIER_DECAY_H

// Decrements ticksRemaining_ on every per-entity Modifier and drops
// expired entries. Runs at the start of the modifier pipeline, before
// any RESOLVE system. Because decay precedes all RESOLVE systems, a
// modifier with ticksRemaining_=1 is removed before its first resolve
// pass — it fires for zero frames. Use ticksRemaining_=2 to fire for
// exactly one frame.
//
// `ticksRemaining_ == -1` is the sentinel for "no decay" — those entries
// are kept untouched.

#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>

#include <algorithm>

namespace IRSystem {

template <> struct System<MODIFIER_DECAY> {
    static SystemId create() {
        return createSystem<IRComponents::C_Modifiers>(
            "ModifierDecay",
            [](IRComponents::C_Modifiers &m) {
                auto &v = m.modifiers_;
                auto newEnd = std::remove_if(
                    v.begin(),
                    v.end(),
                    [](IRComponents::Modifier &mod) {
                        if (mod.ticksRemaining_ == -1) return false;
                        --mod.ticksRemaining_;
                        return mod.ticksRemaining_ <= 0;
                    }
                );
                v.erase(newEnd, v.end());
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_MODIFIER_DECAY_H */
