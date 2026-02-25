#ifndef COMPONENT_ANIM_CLIP_COLOR_TRACK_LUA_H
#define COMPONENT_ANIM_CLIP_COLOR_TRACK_LUA_H

#include <irreden/update/components/component_anim_clip_color_track.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::AnimPhaseColor> = true;

template <> inline void bindLuaType<IRComponents::AnimPhaseColor>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::AnimPhaseColor,
        IRComponents::AnimPhaseColor(IRMath::Color, IRMath::Color, IRMath::IREasingFunctions),
        IRComponents::AnimPhaseColor()>(
        "AnimPhaseColor",
        "startColor",
        &IRComponents::AnimPhaseColor::startColor_,
        "endColor",
        &IRComponents::AnimPhaseColor::endColor_,
        "easingFunction",
        &IRComponents::AnimPhaseColor::easingFunction_
    );
}

template <> inline constexpr bool kHasLuaBinding<IRComponents::AnimPhaseColorMod> = true;

template <> inline void bindLuaType<IRComponents::AnimPhaseColorMod>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::AnimPhaseColorMod,
        IRComponents::AnimPhaseColorMod(IRMath::ColorHSV, IRMath::ColorHSV, IRMath::IREasingFunctions),
        IRComponents::AnimPhaseColorMod()>(
        "AnimPhaseColorMod",
        "startMod",
        &IRComponents::AnimPhaseColorMod::startMod_,
        "endMod",
        &IRComponents::AnimPhaseColorMod::endMod_,
        "easingFunction",
        &IRComponents::AnimPhaseColorMod::easingFunction_
    );
}

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_AnimClipColorTrack> = true;

template <> inline void bindLuaType<IRComponents::C_AnimClipColorTrack>(LuaScript &luaScript) {
    auto type = luaScript.registerType<
        IRComponents::C_AnimClipColorTrack,
        IRComponents::C_AnimClipColorTrack()>(
        "C_AnimClipColorTrack",
        "mode",
        &IRComponents::C_AnimClipColorTrack::mode_,
        "phaseCount",
        &IRComponents::C_AnimClipColorTrack::phaseCount_,
        "idleColor",
        &IRComponents::C_AnimClipColorTrack::idleColor_,
        "idleMod",
        &IRComponents::C_AnimClipColorTrack::idleMod_
    );
    type["addPhaseColor"] = &IRComponents::C_AnimClipColorTrack::addPhaseColor;
    type["addPhaseMod"] = &IRComponents::C_AnimClipColorTrack::addPhaseMod;
}

} // namespace IRScript

#endif /* COMPONENT_ANIM_CLIP_COLOR_TRACK_LUA_H */
