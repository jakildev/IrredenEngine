#ifndef COMPONENT_PARTICLE_BURST_LUA_H
#define COMPONENT_PARTICLE_BURST_LUA_H

#include <irreden/update/components/component_particle_burst.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_ParticleBurst> = true;

template <> inline void bindLuaType<IRComponents::C_ParticleBurst>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_ParticleBurst,
        IRComponents::C_ParticleBurst(int, int, float),
        IRComponents::C_ParticleBurst()>(
        "C_ParticleBurst",
        "count",
        &IRComponents::C_ParticleBurst::count_,
        "lifetime",
        &IRComponents::C_ParticleBurst::lifetime_,
        "speed",
        &IRComponents::C_ParticleBurst::speed_
    );
}
} // namespace IRScript

#endif /* COMPONENT_PARTICLE_BURST_LUA_H */
