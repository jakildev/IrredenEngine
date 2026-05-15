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
                    IRComponents::detail::tickAndExpired<IRComponents::Modifier>
                );
                v.erase(newEnd, v.end());

                auto &v3 = g.modifiersVec3_;
                auto newEnd3 = std::remove_if(
                    v3.begin(),
                    v3.end(),
                    IRComponents::detail::tickAndExpired<IRComponents::ModifierVec3>
                );
                v3.erase(newEnd3, v3.end());
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GLOBAL_MODIFIER_DECAY_H */
