#ifndef COMPONENT_NAV_GRID_LUA_H
#define COMPONENT_NAV_GRID_LUA_H

#include <irreden/update/components/component_nav_grid.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_NavGrid> = true;

template <> inline void bindLuaType<IRComponents::C_NavGrid>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_NavGrid,
        IRComponents::C_NavGrid(float),
        IRComponents::C_NavGrid()>("C_NavGrid");
}
} // namespace IRScript

#endif /* COMPONENT_NAV_GRID_LUA_H */
