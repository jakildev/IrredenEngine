#ifndef COMPONENT_ANIMATION_CLIP_LUA_H
#define COMPONENT_ANIMATION_CLIP_LUA_H

#include <irreden/update/components/component_animation_clip.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_AnimationClip> = true;

template <> inline void bindLuaType<IRComponents::C_AnimationClip>(LuaScript &luaScript) {
    auto type = luaScript.registerType<
        IRComponents::C_AnimationClip,
        IRComponents::C_AnimationClip()>(
        "C_AnimationClip",
        "phaseCount",
        &IRComponents::C_AnimationClip::phaseCount_,
        "actionPhaseIndex",
        &IRComponents::C_AnimationClip::actionPhaseIndex_
    );
    type["addPhase"] = &IRComponents::C_AnimationClip::addPhase;
}

} // namespace IRScript

#endif /* COMPONENT_ANIMATION_CLIP_LUA_H */
