#ifndef COMPONENT_ZOOM_LEVEL_LUA_H
#define COMPONENT_ZOOM_LEVEL_LUA_H

#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/prefab_component_factory.hpp>

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

    // Opts into declarative prefab spawning — `components = { C_ZoomLevel
    // = { zoom = 5.0 } }` in a prefab file applies the override on top of
    // the default-constructed C_ZoomLevel. The scalar `zoom` expands to
    // `vec2(zoom, zoom)` via the matching `C_ZoomLevel(float)` ctor;
    // callers wanting independent x/y can swap in a `{x, y}` table once
    // a vec2-from-Lua helper lands. See engine/script/CLAUDE.md
    // "Prefab format → declarative components" for the wiring contract.
    registerComponentFactoryFor<IRComponents::C_ZoomLevel>(
        "C_ZoomLevel",
        [](IRComponents::C_ZoomLevel &c, const sol::table &t) {
            sol::optional<float> zoom = t["zoom"];
            if (zoom) {
                c = IRComponents::C_ZoomLevel{*zoom};
            }
        }
    );
}
} // namespace IRScript

#endif /* COMPONENT_ZOOM_LEVEL_LUA_H */
