#ifndef COMPONENT_SPRING_PLATFORM_LUA_H
#define COMPONENT_SPRING_PLATFORM_LUA_H

#include <irreden/update/components/component_spring_platform.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <>
inline constexpr bool kHasLuaBinding<IRComponents::C_SpringPlatform> = true;

template <>
inline void bindLuaType<IRComponents::C_SpringPlatform>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_SpringPlatform,
        IRComponents::C_SpringPlatform(
            float, float, float, float, float, float, int, int, float, float,
            IRMath::vec3, IRMath::ColorHSV, IRMath::ColorHSV, float, float),
        IRComponents::C_SpringPlatform()>(
        "C_SpringPlatform",
        "stiffness",
        &IRComponents::C_SpringPlatform::stiffness_,
        "damping",
        &IRComponents::C_SpringPlatform::damping_,
        "length",
        &IRComponents::C_SpringPlatform::length_,
        "lockRatio",
        &IRComponents::C_SpringPlatform::lockRatio_,
        "overshootRatio",
        &IRComponents::C_SpringPlatform::overshootRatio_,
        "absorptionRatio",
        &IRComponents::C_SpringPlatform::absorptionRatio_,
        "maxLaunchOscillations",
        &IRComponents::C_SpringPlatform::maxLaunchOscillations_,
        "maxCatchOscillations",
        &IRComponents::C_SpringPlatform::maxCatchOscillations_,
        "settleSpeed",
        &IRComponents::C_SpringPlatform::settleSpeed_,
        "loadLeadSeconds",
        &IRComponents::C_SpringPlatform::loadLeadSeconds_,
        "direction",
        &IRComponents::C_SpringPlatform::direction_,
        "lockColorShift",
        &IRComponents::C_SpringPlatform::lockColorShift_,
        "releaseColorShift",
        &IRComponents::C_SpringPlatform::releaseColorShift_,
        "colorMinValue",
        &IRComponents::C_SpringPlatform::colorMinValue_,
        "colorMinSaturation",
        &IRComponents::C_SpringPlatform::colorMinSaturation_
    );
}
} // namespace IRScript

#endif /* COMPONENT_SPRING_PLATFORM_LUA_H */
