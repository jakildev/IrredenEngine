#ifndef COMPONENT_SPAWN_GLOW_LUA_H
#define COMPONENT_SPAWN_GLOW_LUA_H

#include <irreden/update/components/component_spawn_glow.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_SpawnGlow> = true;

template <> inline void bindLuaType<IRComponents::C_SpawnGlow>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_SpawnGlow,
        IRComponents::C_SpawnGlow(IRMath::Color, IRMath::Color, float, float, IRMath::IREasingFunctions),
        IRComponents::C_SpawnGlow()>(
        "C_SpawnGlow",
        "baseColor",
        &IRComponents::C_SpawnGlow::baseColor_,
        "targetColor",
        &IRComponents::C_SpawnGlow::targetColor_,
        "holdSeconds",
        &IRComponents::C_SpawnGlow::holdSeconds_,
        "fadeSeconds",
        &IRComponents::C_SpawnGlow::fadeSeconds_,
        "easingFunction",
        &IRComponents::C_SpawnGlow::easingFunction_
    );
}
} // namespace IRScript

#endif /* COMPONENT_SPAWN_GLOW_LUA_H */
