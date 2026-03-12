#ifndef COMPONENT_NAV_CELL_LUA_H
#define COMPONENT_NAV_CELL_LUA_H

#include <irreden/update/components/component_nav_cell.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_NavCell> = true;

template <> inline void bindLuaType<IRComponents::C_NavCell>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_NavCell,
        IRComponents::C_NavCell(IRMath::ivec3, bool, float),
        IRComponents::C_NavCell(IRMath::ivec3, bool),
        IRComponents::C_NavCell()>(
        "C_NavCell",
        "gridPos",
        &IRComponents::C_NavCell::gridPos_,
        "passable",
        &IRComponents::C_NavCell::passable_,
        "clearance",
        &IRComponents::C_NavCell::clearance_
    );
}
} // namespace IRScript

#endif /* COMPONENT_NAV_CELL_LUA_H */
