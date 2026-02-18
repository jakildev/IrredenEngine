#ifndef COMPONENT_PERIODIC_IDLE_LUA_H
#define COMPONENT_PERIODIC_IDLE_LUA_H

#include <irreden/update/components/component_periodic_idle.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::PeriodStage> = true;

template <> inline void bindLuaType<IRComponents::PeriodStage>(LuaScript &luaScript) {
    luaScript.registerType<IRComponents::PeriodStage,
                           IRComponents::PeriodStage(float, float, float, float, IREasingFunctions,
                                                     bool)>("PeriodStage");
}

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_PeriodicIdle> = true;

template <> inline void bindLuaType<IRComponents::C_PeriodicIdle>(LuaScript &luaScript) {
    luaScript.registerType<IRComponents::C_PeriodicIdle,
                           IRComponents::C_PeriodicIdle(float, float, float),
                           IRComponents::C_PeriodicIdle(IRMath::vec3, float, float)>(
        "C_PeriodicIdle", "addStageDurationSeconds",
        &IRComponents::C_PeriodicIdle::addStageDurationSeconds);
}
} // namespace IRScript

#endif /* COMPONENT_PERIODIC_IDLE_LUA_H */
