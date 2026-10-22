#ifndef COMPONENT_MOUSE_SCROLL_LUA_H
#define COMPONENT_MOUSE_SCROLL_LUA_H

#include <irreden/input/components/component_mouse_scroll.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_MouseScroll> = true;

template <> inline void bindLuaType<IRComponents::C_MouseScroll>(LuaScript &luaScript) {
    using IRComponents::C_MouseScroll;
    luaScript.registerType<C_MouseScroll, C_MouseScroll(), C_MouseScroll(double, double)>(
        "C_MouseScroll",
        "xoffset",
        &C_MouseScroll::xoffset_,
        "yoffset",
        &C_MouseScroll::yoffset_
    );
}
} // namespace IRScript

#endif /* COMPONENT_MOUSE_SCROLL_LUA_H */
