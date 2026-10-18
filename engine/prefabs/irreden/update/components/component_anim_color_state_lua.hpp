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
        &IRComponents::C_AnimColorState::blendMode_,
        // The colour the shifts start from and the one painted each frame.
        // The system captures the base from the voxels on its first tick; a
        // script that recolours the voxels afterwards sets both (and marks
        // the base captured), or the next tick paints the old colour back.
        "baseColor",
        &IRComponents::C_AnimColorState::baseColor_,
        "baseInitialized",
        &IRComponents::C_AnimColorState::baseInitialized_,
        "currentColor",
        &IRComponents::C_AnimColorState::currentColor_
    );
}

} // namespace IRScript

#endif /* COMPONENT_ANIM_COLOR_STATE_LUA_H */
