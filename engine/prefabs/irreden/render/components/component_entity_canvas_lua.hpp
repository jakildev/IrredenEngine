#ifndef COMPONENT_ENTITY_CANVAS_LUA_H
#define COMPONENT_ENTITY_CANVAS_LUA_H

#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_EntityCanvas> = true;

template <> inline void bindLuaType<IRComponents::C_EntityCanvas>(LuaScript &luaScript) {
    using IRComponents::C_EntityCanvas;
    // Default ctor only — to create a live canvas-backed entity call IRPrefab::EntityCanvas::create() from C++; no Lua factory binding exists.
    luaScript.registerType<C_EntityCanvas, C_EntityCanvas()>(
        "C_EntityCanvas",
        "canvasSize",
        &C_EntityCanvas::canvasSize_,
        "visible",
        &C_EntityCanvas::visible_,
        "canvasEntity",
        &C_EntityCanvas::canvasEntity_
    );
}
} // namespace IRScript

#endif /* COMPONENT_ENTITY_CANVAS_LUA_H */
