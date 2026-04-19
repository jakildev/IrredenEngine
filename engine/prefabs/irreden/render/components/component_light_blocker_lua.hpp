#ifndef COMPONENT_LIGHT_BLOCKER_LUA_H
#define COMPONENT_LIGHT_BLOCKER_LUA_H

#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_LightBlocker> = true;

template <> inline void bindLuaType<IRComponents::C_LightBlocker>(LuaScript &luaScript) {
    using IRComponents::C_LightBlocker;

    luaScript.registerType<
        C_LightBlocker,
        C_LightBlocker(bool, bool, float),
        C_LightBlocker()>(
        "C_LightBlocker",
        "blocksLOS",
        &C_LightBlocker::blocksLOS_,
        "castsShadow",
        &C_LightBlocker::castsShadow_,
        "opacity",
        &C_LightBlocker::opacity_
    );
}

} // namespace IRScript

#endif /* COMPONENT_LIGHT_BLOCKER_LUA_H */
