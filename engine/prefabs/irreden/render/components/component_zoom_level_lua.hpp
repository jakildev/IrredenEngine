#ifndef COMPONENT_ZOOM_LEVEL_LUA_H
#define COMPONENT_ZOOM_LEVEL_LUA_H

#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_ZoomLevel> = true;

template <> inline void bindLuaType<IRComponents::C_ZoomLevel>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_ZoomLevel,
        IRComponents::C_ZoomLevel(float),
        IRComponents::C_ZoomLevel()>(
        "C_ZoomLevel",
        "zoom",
        &IRComponents::C_ZoomLevel::zoom_,
        "zoomIn",
        &IRComponents::C_ZoomLevel::zoomIn,
        "zoomOut",
        &IRComponents::C_ZoomLevel::zoomOut
    );
}
} // namespace IRScript

#endif /* COMPONENT_ZOOM_LEVEL_LUA_H */
