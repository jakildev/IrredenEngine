#ifndef COMPONENT_AUTO_SPIN_LUA_H
#define COMPONENT_AUTO_SPIN_LUA_H

#include <irreden/common/components/component_auto_spin.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_AutoSpin> = true;

template <> inline void bindLuaType<IRComponents::C_AutoSpin>(LuaScript &luaScript) {
    using IRComponents::C_AutoSpin;
    sol::usertype<C_AutoSpin> type =
        luaScript.registerType<C_AutoSpin, C_AutoSpin(), C_AutoSpin(IRMath::vec3, float)>(
            "C_AutoSpin",
            "radiansPerFrame",
            &C_AutoSpin::radiansPerFrame_
        );

    // Added after registerType, on the returned usertype — see
    // component_local_transform_lua.hpp for why a property can't ride the
    // key/value pack.
    type["axis"] = sol::property(
        [](C_AutoSpin &obj, sol::this_state ts) {
            return sol::state_view{ts}
                .create_table_with("x", obj.axis_.x, "y", obj.axis_.y, "z", obj.axis_.z);
        },
        [](C_AutoSpin &obj, sol::object value) { obj.axis_ = IRScript::vec3FromLua(value); }
    );
}
} // namespace IRScript

#endif /* COMPONENT_AUTO_SPIN_LUA_H */
