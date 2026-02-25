#ifndef COMPONENT_ANIM_MOTION_COLOR_SHIFT_LUA_H
#define COMPONENT_ANIM_MOTION_COLOR_SHIFT_LUA_H

#include <irreden/update/components/component_anim_motion_color_shift.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_AnimMotionColorShift> = true;

template <> inline void bindLuaType<IRComponents::C_AnimMotionColorShift>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_AnimMotionColorShift,
        IRComponents::C_AnimMotionColorShift(IRMath::Color, float, float),
        IRComponents::C_AnimMotionColorShift()>(
        "C_AnimMotionColorShift",
        "motionColor",
        &IRComponents::C_AnimMotionColorShift::motionColor_,
        "fadeInSpeed",
        &IRComponents::C_AnimMotionColorShift::fadeInSpeed_,
        "fadeOutSpeed",
        &IRComponents::C_AnimMotionColorShift::fadeOutSpeed_,
        "currentBlend",
        &IRComponents::C_AnimMotionColorShift::currentBlend_
    );
}

} // namespace IRScript

#endif /* COMPONENT_ANIM_MOTION_COLOR_SHIFT_LUA_H */
