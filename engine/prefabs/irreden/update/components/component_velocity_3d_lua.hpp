#ifndef COMPONENT_VELOCITY_3D_LUA_H
#define COMPONENT_VELOCITY_3D_LUA_H

#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_Velocity3D> = true;

template <> inline void bindLuaType<IRComponents::C_Velocity3D>(LuaScript &luaScript) {
    luaScript.registerType<IRComponents::C_Velocity3D, float, float, float>("C_Velocity3D");
}
} // namespace IRScript

#endif /* COMPONENT_VELOCITY_3D_LUA_H */
