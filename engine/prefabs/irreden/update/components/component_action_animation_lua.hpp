#ifndef COMPONENT_ACTION_ANIMATION_LUA_H
#define COMPONENT_ACTION_ANIMATION_LUA_H

#include <irreden/update/components/component_action_animation.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/ir_script_types.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::ActionAnimationPhase> = true;

template <> inline void bindLuaType<IRComponents::ActionAnimationPhase>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::ActionAnimationPhase,
        IRComponents::ActionAnimationPhase(double, float, float, IRMath::IREasingFunctions),
        IRComponents::ActionAnimationPhase()>(
        "ActionAnimationPhase",
        "durationSeconds",
        &IRComponents::ActionAnimationPhase::durationSeconds_,
        "startDisplacement",
        &IRComponents::ActionAnimationPhase::startDisplacement_,
        "endDisplacement",
        &IRComponents::ActionAnimationPhase::endDisplacement_,
        "easingFunction",
        &IRComponents::ActionAnimationPhase::easingFunction_
    );
}

template <> inline constexpr bool kHasLuaBinding<IRComponents::AnimationBinding> = true;

template <> inline void bindLuaType<IRComponents::AnimationBinding>(LuaScript &luaScript) {
    auto type = luaScript.lua().new_usertype<IRComponents::AnimationBinding>(
        "AnimationBinding",
        sol::call_constructor, sol::factories(
            [](IRComponents::ActionAnimationTriggerMode trigger,
               const LuaEntity &clipEntity,
               double timerSyncLeadSeconds,
               bool canInterrupt) -> IRComponents::AnimationBinding {
                return IRComponents::AnimationBinding{
                    trigger, clipEntity.entity, timerSyncLeadSeconds, canInterrupt};
            },
            []() -> IRComponents::AnimationBinding {
                return IRComponents::AnimationBinding{};
            }
        ),
        "trigger", &IRComponents::AnimationBinding::trigger_,
        "clipEntity", &IRComponents::AnimationBinding::clipEntity_,
        "timerSyncLeadSeconds", &IRComponents::AnimationBinding::timerSyncLeadSeconds_,
        "canInterrupt", &IRComponents::AnimationBinding::canInterrupt_
    );
    type["new"] = sol::overload(
        [](IRComponents::ActionAnimationTriggerMode trigger,
           const LuaEntity &clipEntity,
           double timerSyncLeadSeconds,
           bool canInterrupt) -> IRComponents::AnimationBinding {
            return IRComponents::AnimationBinding{
                trigger, clipEntity.entity, timerSyncLeadSeconds, canInterrupt};
        },
        []() -> IRComponents::AnimationBinding {
            return IRComponents::AnimationBinding{};
        }
    );
}

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_ActionAnimation> = true;

template <> inline void bindLuaType<IRComponents::C_ActionAnimation>(LuaScript &luaScript) {
    auto type = luaScript.registerType<
        IRComponents::C_ActionAnimation,
        IRComponents::C_ActionAnimation()>(
        "C_ActionAnimation",
        "direction",
        &IRComponents::C_ActionAnimation::direction_,
        "currentDisplacement",
        &IRComponents::C_ActionAnimation::currentDisplacement_,
        "bindingCount",
        &IRComponents::C_ActionAnimation::bindingCount_,
        "actionFired",
        &IRComponents::C_ActionAnimation::actionFired_
    );
    type["addBinding"] = &IRComponents::C_ActionAnimation::addBinding;
}

} // namespace IRScript

#endif /* COMPONENT_ACTION_ANIMATION_LUA_H */
