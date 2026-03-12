#ifndef COMPONENT_SELECTED_LUA_H
#define COMPONENT_SELECTED_LUA_H

#include <irreden/common/components/component_selected.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_Selected> = true;

template <> inline void bindLuaType<IRComponents::C_Selected>(LuaScript &luaScript) {
    luaScript.registerType<IRComponents::C_Selected, IRComponents::C_Selected()>(
        "C_Selected"
    );
}
} // namespace IRScript

#endif /* COMPONENT_SELECTED_LUA_H */
