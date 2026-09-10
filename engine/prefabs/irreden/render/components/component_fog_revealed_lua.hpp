#ifndef COMPONENT_FOG_REVEALED_LUA_H
#define COMPONENT_FOG_REVEALED_LUA_H

#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_FogRevealed> = true;

template <> inline void bindLuaType<IRComponents::C_FogRevealed>(LuaScript &luaScript) {
    using IRComponents::C_FogRevealed;
    luaScript.registerType<C_FogRevealed, C_FogRevealed()>(
        "C_FogRevealed",
        "revealFactor",
        &C_FogRevealed::revealFactor_,
        "shown",
        &C_FogRevealed::shown_
    );
}

} // namespace IRScript

#endif /* COMPONENT_FOG_REVEALED_LUA_H */
