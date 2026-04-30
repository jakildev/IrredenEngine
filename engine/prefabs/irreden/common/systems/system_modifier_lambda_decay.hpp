#ifndef SYSTEM_MODIFIER_LAMBDA_DECAY_H
#define SYSTEM_MODIFIER_LAMBDA_DECAY_H

// Per-entity lambda counterpart to MODIFIER_DECAY. Pruning expired
// entries fires the captured callable's destructor (sol::function
// teardown calls lua_unref into the LuaScript that owns it — the
// LuaScript must outlive every entity carrying a lambda modifier;
// production already guarantees this since LuaScript outlives the
// EntityManager).

#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_modifiers.hpp>

#include <algorithm>

namespace IRSystem {

template <> struct System<LAMBDA_MODIFIER_DECAY> {
    static SystemId create() {
        return createSystem<IRComponents::C_LambdaModifiers>(
            "LambdaModifierDecay",
            [](IRComponents::C_LambdaModifiers &m) {
                auto &v = m.modifiers_;
                auto newEnd = std::remove_if(
                    v.begin(), v.end(), IRComponents::detail::tickAndExpired<IRComponents::LambdaModifier>
                );
                v.erase(newEnd, v.end());
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_MODIFIER_LAMBDA_DECAY_H */
