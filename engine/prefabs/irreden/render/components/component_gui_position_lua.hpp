#ifndef COMPONENT_GUI_POSITION_LUA_H
#define COMPONENT_GUI_POSITION_LUA_H

#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_GuiPosition> = true;

template <> inline void bindLuaType<IRComponents::C_GuiPosition>(LuaScript &luaScript) {
    using IRComponents::C_GuiPosition;
    luaScript.registerType<C_GuiPosition, C_GuiPosition(int, int)>(
        "C_GuiPosition",
        "x",
        [](C_GuiPosition &obj) { return obj.pos_.x; },
        "y",
        [](C_GuiPosition &obj) { return obj.pos_.y; }
    );
}
} // namespace IRScript

#endif /* COMPONENT_GUI_POSITION_LUA_H */
