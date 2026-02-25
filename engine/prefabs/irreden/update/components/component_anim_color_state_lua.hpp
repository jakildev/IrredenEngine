#ifndef COMPONENT_ANIM_COLOR_STATE_LUA_H
#define COMPONENT_ANIM_COLOR_STATE_LUA_H

#include <irreden/update/components/component_anim_color_state.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_AnimColorState> = true;

template <> inline void bindLuaType<IRComponents::C_AnimColorState>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_AnimColorState,
        IRComponents::C_AnimColorState(IRComponents::AnimColorBlendMode),
        IRComponents::C_AnimColorState()>(
        "C_AnimColorState",
        "blendMode",
        &IRComponents::C_AnimColorState::blendMode_
    );
}

} // namespace IRScript

#endif /* COMPONENT_ANIM_COLOR_STATE_LUA_H */
