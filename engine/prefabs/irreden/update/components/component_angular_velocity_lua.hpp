#ifndef COMPONENT_ANGULAR_VELOCITY_LUA_H
#define COMPONENT_ANGULAR_VELOCITY_LUA_H

#include <irreden/update/components/component_angular_velocity.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_AngularVelocity> = true;

template <> inline void bindLuaType<IRComponents::C_AngularVelocity>(LuaScript &luaScript) {
    using IRComponents::C_AngularVelocity;
    sol::usertype<C_AngularVelocity> type = luaScript.registerType<
        C_AngularVelocity,
        C_AngularVelocity(),
        C_AngularVelocity(IRMath::vec3, float),
        C_AngularVelocity(IRMath::vec3, float, float)>(
        "C_AngularVelocity",
        "radiansPerFrame",
        &C_AngularVelocity::radiansPerFrame_,
        "dampingPerFrame",
        &C_AngularVelocity::dampingPerFrame_,
        // `spin:impulse(axis, radiansPerFrame)` — `axis` is a `{x,y,z}` table
        // or a vec3 userdata.
        "impulse",
        [](C_AngularVelocity &obj, sol::object axis, float radiansPerFrame) {
            obj.impulse(IRScript::vec3FromLua(axis), radiansPerFrame);
        }
    );

    // Added after registerType, on the returned usertype — see
    // component_local_transform_lua.hpp for why a property can't ride the
    // key/value pack.
    type["axis"] = sol::property(
        [](C_AngularVelocity &obj, sol::this_state ts) {
            return sol::state_view{ts}
                .create_table_with("x", obj.axis_.x, "y", obj.axis_.y, "z", obj.axis_.z);
        },
        [](C_AngularVelocity &obj, sol::object value) { obj.axis_ = IRScript::vec3FromLua(value); }
    );
}
} // namespace IRScript

#endif /* COMPONENT_ANGULAR_VELOCITY_LUA_H */
