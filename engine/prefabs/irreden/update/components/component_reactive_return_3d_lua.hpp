#ifndef COMPONENT_REACTIVE_RETURN_3D_LUA_H
#define COMPONENT_REACTIVE_RETURN_3D_LUA_H

#include <irreden/update/components/component_reactive_return_3d.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_ReactiveReturn3D> = true;

template <> inline void bindLuaType<IRComponents::C_ReactiveReturn3D>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_ReactiveReturn3D,
        IRComponents::C_ReactiveReturn3D(IRMath::vec3, float, float, int, float, float, bool),
        IRComponents::C_ReactiveReturn3D()>(
        "C_ReactiveReturn3D",
        "triggerImpulseVelocity",
        &IRComponents::C_ReactiveReturn3D::triggerImpulseVelocity_,
        "springStrength",
        &IRComponents::C_ReactiveReturn3D::springStrength_,
        "dampingPerSecond",
        &IRComponents::C_ReactiveReturn3D::dampingPerSecond_,
        "maxRebounds",
        &IRComponents::C_ReactiveReturn3D::maxRebounds_,
        "settleDistance",
        &IRComponents::C_ReactiveReturn3D::settleDistance_,
        "settleSpeed",
        &IRComponents::C_ReactiveReturn3D::settleSpeed_,
        "triggerOnContactEnter",
        &IRComponents::C_ReactiveReturn3D::triggerOnContactEnter_
    );
}
} // namespace IRScript

#endif /* COMPONENT_REACTIVE_RETURN_3D_LUA_H */
