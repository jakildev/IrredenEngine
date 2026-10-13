#ifndef COMPONENT_AUTO_SPIN_LUA_H
#define COMPONENT_AUTO_SPIN_LUA_H

#include <irreden/common/components/component_auto_spin.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/prefab_component_factory.hpp>

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

    // A prefab's `components = { C_AutoSpin = { axis = { 0, 0, 1 },
    // radiansPerFrame = 0.02 } }`. An omitted field keeps its default, so
    // `{ radiansPerFrame = r }` spins about +z.
    registerComponentFactoryFor<C_AutoSpin>("C_AutoSpin", [](C_AutoSpin &c, const sol::table &t) {
        const sol::object axis = t["axis"];
        if (axis.valid() && axis.get_type() != sol::type::lua_nil) {
            c.axis_ = IRScript::vec3FromLua(axis);
        }
        const sol::optional<float> radiansPerFrame = t["radiansPerFrame"];
        if (radiansPerFrame) {
            c.radiansPerFrame_ = *radiansPerFrame;
        }
    });
}
} // namespace IRScript

#endif /* COMPONENT_AUTO_SPIN_LUA_H */
