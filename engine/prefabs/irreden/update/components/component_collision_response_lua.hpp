#ifndef COMPONENT_COLLISION_RESPONSE_LUA_H
#define COMPONENT_COLLISION_RESPONSE_LUA_H

#include <irreden/update/components/component_collision_response.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_CollisionResponse> = true;

// Restitution and friction are constructor-only: a writable member pointer
// would bypass the [0, 1] clamp, and a restitution above 1 adds energy.
template <> inline void bindLuaType<IRComponents::C_CollisionResponse>(LuaScript &luaScript) {
    using IRComponents::C_CollisionResponse;
    sol::usertype<C_CollisionResponse> type = luaScript.registerType<
        C_CollisionResponse,
        C_CollisionResponse(),
        C_CollisionResponse(float, float, bool),
        C_CollisionResponse(float, float, bool, float)>("C_CollisionResponse");
    type["restSpeed"] = sol::property(
        [](const C_CollisionResponse &obj) { return obj.restSpeed_; },
        [](C_CollisionResponse &obj, float restSpeed) {
            obj.restSpeed_ = IRMath::max(restSpeed, 0.0f);
        }
    );
    type["resting"] = sol::readonly(&C_CollisionResponse::resting_);
}
} // namespace IRScript

#endif /* COMPONENT_COLLISION_RESPONSE_LUA_H */
