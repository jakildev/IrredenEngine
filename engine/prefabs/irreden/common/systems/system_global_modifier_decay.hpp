#ifndef SYSTEM_GLOBAL_MODIFIER_DECAY_H
#define SYSTEM_GLOBAL_MODIFIER_DECAY_H

// Same decay logic as MODIFIER_DECAY but for the singleton's
// C_GlobalModifiers vector. Globals live on a single named entity
// ("modifierGlobals") created by `registerResolverPipeline`. Archetype
// iteration handles the singleton naturally — only one entity in the
// world carries C_GlobalModifiers, so the tick fires once per frame.

#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>

#include <algorithm>

namespace IRSystem {

template <> struct System<GLOBAL_MODIFIER_DECAY> {
    static SystemId create() {
        return createSystem<IRComponents::C_GlobalModifiers>(
            "GlobalModifierDecay",
            [](IRComponents::C_GlobalModifiers &g) {
                auto &v = g.modifiers_;
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

#endif /* SYSTEM_GLOBAL_MODIFIER_DECAY_H */
