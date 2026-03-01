#ifndef COMPONENT_RHYTHMIC_LAUNCH_LUA_H
#define COMPONENT_RHYTHMIC_LAUNCH_LUA_H

#include <irreden/update/components/component_rhythmic_launch.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_RhythmicLaunch> = true;

template <> inline void bindLuaType<IRComponents::C_RhythmicLaunch>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_RhythmicLaunch,
        IRComponents::C_RhythmicLaunch(float, IRMath::vec3, float, float, bool, int32_t),
        IRComponents::C_RhythmicLaunch(float, IRMath::vec3, float, float, bool),
        IRComponents::C_RhythmicLaunch(float, IRMath::vec3, float, float),
        IRComponents::C_RhythmicLaunch(float, IRMath::vec3, float),
        IRComponents::C_RhythmicLaunch()>(
        "C_RhythmicLaunch",
        "periodSeconds",
        &IRComponents::C_RhythmicLaunch::periodSeconds_,
        "impulseVelocity",
        &IRComponents::C_RhythmicLaunch::impulseVelocity_,
        "restOffsetZ",
        &IRComponents::C_RhythmicLaunch::restOffsetZ_,
        "elapsedSeconds",
        &IRComponents::C_RhythmicLaunch::elapsedSeconds_,
        "grounded",
        &IRComponents::C_RhythmicLaunch::grounded_,
        "maxLaunches",
        &IRComponents::C_RhythmicLaunch::maxLaunches_,
        "launchCount",
        &IRComponents::C_RhythmicLaunch::launchCount_
    );
}
} // namespace IRScript

#endif /* COMPONENT_RHYTHMIC_LAUNCH_LUA_H */
