#ifndef COMPONENT_PARTICLE_BURST_LUA_H
#define COMPONENT_PARTICLE_BURST_LUA_H

#include <irreden/update/components/component_particle_burst.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_ParticleBurst> = true;

template <> inline void bindLuaType<IRComponents::C_ParticleBurst>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_ParticleBurst,
        IRComponents::C_ParticleBurst(int, int, float, float, float, float, float, float, float),
        IRComponents::C_ParticleBurst(int, int, float, float, float, float, float, float),
        IRComponents::C_ParticleBurst(int, int, float, float, float, float, float),
        IRComponents::C_ParticleBurst(int, int, float, float),
        IRComponents::C_ParticleBurst(int, int, float),
        IRComponents::C_ParticleBurst()>(
        "C_ParticleBurst",
        "count",
        &IRComponents::C_ParticleBurst::count_,
        "lifetime",
        &IRComponents::C_ParticleBurst::lifetime_,
        "speed",
        &IRComponents::C_ParticleBurst::speed_,
        "upwardAcceleration",
        &IRComponents::C_ParticleBurst::upwardAcceleration_,
        "dragScaleX",
        &IRComponents::C_ParticleBurst::dragScaleX_,
        "dragScaleY",
        &IRComponents::C_ParticleBurst::dragScaleY_,
        "dragScaleZ",
        &IRComponents::C_ParticleBurst::dragScaleZ_,
        "spawnOffsetZ",
        &IRComponents::C_ParticleBurst::spawnOffsetZ_,
        "isoDepthOffset",
        &IRComponents::C_ParticleBurst::isoDepthOffset_,
        "xySpeedRatio",
        &IRComponents::C_ParticleBurst::xySpeedRatio_,
        "zSpeedRatio",
        &IRComponents::C_ParticleBurst::zSpeedRatio_,
        "zVarianceRatio",
        &IRComponents::C_ParticleBurst::zVarianceRatio_,
        "pDragPerSecond",
        &IRComponents::C_ParticleBurst::pDragPerSecond_,
        "pDriftDelaySeconds",
        &IRComponents::C_ParticleBurst::pDriftDelaySeconds_,
        "pDriftUpAccelPerSec",
        &IRComponents::C_ParticleBurst::pDriftUpAccelPerSec_,
        "pDragMinSpeed",
        &IRComponents::C_ParticleBurst::pDragMinSpeed_,
        "pHoverDurationSec",
        &IRComponents::C_ParticleBurst::pHoverDurationSec_,
        "pHoverOscSpeed",
        &IRComponents::C_ParticleBurst::pHoverOscSpeed_,
        "pHoverOscAmplitude",
        &IRComponents::C_ParticleBurst::pHoverOscAmplitude_,
        "pHoverBlendSec",
        &IRComponents::C_ParticleBurst::pHoverBlendSec_,
        "pHoverBlendEasing",
        &IRComponents::C_ParticleBurst::pHoverBlendEasing_,
        "hoverStartVariance",
        &IRComponents::C_ParticleBurst::hoverStartVariance_,
        "hoverDurationVariance",
        &IRComponents::C_ParticleBurst::hoverDurationVariance_,
        "hoverAmplitudeVariance",
        &IRComponents::C_ParticleBurst::hoverAmplitudeVariance_,
        "hoverSpeedVariance",
        &IRComponents::C_ParticleBurst::hoverSpeedVariance_,
        "glowEnabled",
        &IRComponents::C_ParticleBurst::glowEnabled_,
        "glowColor",
        &IRComponents::C_ParticleBurst::glowColor_,
        "glowHoldSeconds",
        &IRComponents::C_ParticleBurst::glowHoldSeconds_,
        "glowFadeSeconds",
        &IRComponents::C_ParticleBurst::glowFadeSeconds_,
        "glowEasing",
        &IRComponents::C_ParticleBurst::glowEasing_,
        "gravityEnabled",
        &IRComponents::C_ParticleBurst::gravityEnabled_,
        "downward",
        &IRComponents::C_ParticleBurst::downward_
    );
}
} // namespace IRScript

#endif /* COMPONENT_PARTICLE_BURST_LUA_H */
