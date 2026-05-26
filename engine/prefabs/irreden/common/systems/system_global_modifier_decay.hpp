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
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    void tick(IRComponents::C_GlobalModifiers &g) {
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

        auto &vq = g.modifiersQuat_;
        auto newEndQ = std::remove_if(
            vq.begin(),
            vq.end(),
            IRComponents::detail::tickAndExpired<IRComponents::ModifierQuat>
        );
        vq.erase(newEndQ, vq.end());
    }

    static SystemId create() {
        return registerSystem<GLOBAL_MODIFIER_DECAY, IRComponents::C_GlobalModifiers>(
            "GlobalModifierDecay"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_GLOBAL_MODIFIER_DECAY_H */
