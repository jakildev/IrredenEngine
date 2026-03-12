#ifndef COMPONENT_CONTROLLABLE_UNIT_LUA_H
#define COMPONENT_CONTROLLABLE_UNIT_LUA_H

#include <irreden/common/components/component_controllable_unit.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_ControllableUnit> = true;

template <> inline void bindLuaType<IRComponents::C_ControllableUnit>(LuaScript &luaScript) {
    luaScript.registerType<IRComponents::C_ControllableUnit, IRComponents::C_ControllableUnit()>(
        "C_ControllableUnit"
    );
}
} // namespace IRScript

#endif /* COMPONENT_CONTROLLABLE_UNIT_LUA_H */
