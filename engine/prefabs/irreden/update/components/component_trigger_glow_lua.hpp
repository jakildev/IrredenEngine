#ifndef COMPONENT_TRIGGER_GLOW_LUA_H
#define COMPONENT_TRIGGER_GLOW_LUA_H

#include <irreden/update/components/component_trigger_glow.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_TriggerGlow> = true;

template <> inline void bindLuaType<IRComponents::C_TriggerGlow>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_TriggerGlow,
        IRComponents::C_TriggerGlow(IRMath::Color, float, float, IRMath::IREasingFunctions, bool),
        IRComponents::C_TriggerGlow()>(
        "C_TriggerGlow",
        "targetColor",
        &IRComponents::C_TriggerGlow::targetColor_,
        // The colour the glow eases back to and repaints while idle. The
        // system captures it from the voxels on its first tick; a script that
        // recolours the voxels afterwards sets it too (and marks it captured),
        // or the next idle tick paints the old colour back.
        "baseColor",
        &IRComponents::C_TriggerGlow::baseColor_,
        "baseColorInitialized",
        &IRComponents::C_TriggerGlow::baseColorInitialized_,
        "holdSeconds",
        &IRComponents::C_TriggerGlow::holdSeconds_,
        "fadeSeconds",
        &IRComponents::C_TriggerGlow::fadeSeconds_,
        "easingFunction",
        &IRComponents::C_TriggerGlow::easingFunction_,
        "triggerOnContactEnter",
        &IRComponents::C_TriggerGlow::triggerOnContactEnter_
    );
}
} // namespace IRScript

#endif /* COMPONENT_TRIGGER_GLOW_LUA_H */
