#ifndef COMPONENT_COLLIDER_CIRCLE_LUA_H
#define COMPONENT_COLLIDER_CIRCLE_LUA_H

#include <irreden/update/components/component_collider_circle.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_ColliderCircle> = true;

template <> inline void bindLuaType<IRComponents::C_ColliderCircle>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_ColliderCircle,
        IRComponents::C_ColliderCircle(float),
        IRComponents::C_ColliderCircle(float, float),
        IRComponents::C_ColliderCircle(float, float, float),
        IRComponents::C_ColliderCircle(float, float, IRMath::vec3),
        IRComponents::C_ColliderCircle(float, float, float, IRMath::vec3),
        IRComponents::C_ColliderCircle(float, IRMath::vec3),
        IRComponents::C_ColliderCircle()>(
        "C_ColliderCircle",
        "radius",
        &IRComponents::C_ColliderCircle::radius_,
        "movementCollisionRadius",
        &IRComponents::C_ColliderCircle::movementCollisionRadius_,
        "preferredMovementRadius",
        &IRComponents::C_ColliderCircle::preferredMovementRadius_,
        "centerOffset",
        &IRComponents::C_ColliderCircle::centerOffset_
    );
}
} // namespace IRScript

#endif /* COMPONENT_COLLIDER_CIRCLE_LUA_H */
