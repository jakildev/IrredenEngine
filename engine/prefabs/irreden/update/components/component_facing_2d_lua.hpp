#ifndef COMPONENT_FACING_2D_LUA_H
#define COMPONENT_FACING_2D_LUA_H

#include <irreden/update/components/component_facing_2d.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_Facing2D> = true;

template <> inline void bindLuaType<IRComponents::C_Facing2D>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_Facing2D,
        IRComponents::C_Facing2D(float),
        IRComponents::C_Facing2D(float, float),
        IRComponents::C_Facing2D()>(
        "C_Facing2D",
        "angle",
        &IRComponents::C_Facing2D::angle_,
        "turnRate",
        &IRComponents::C_Facing2D::turnRate_
    );
}
} // namespace IRScript

#endif /* COMPONENT_FACING_2D_LUA_H */
