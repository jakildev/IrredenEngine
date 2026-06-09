#ifndef LUA_RENDER_BINDINGS_H
#define LUA_RENDER_BINDINGS_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

namespace IRScript::detail {

// IRRender + IRGui shared Lua bindings (engine #1615).
//
// Render-glue setters (sun direction / intensity / ambient, sky color /
// intensity) and a minimal GUI-canvas shape-draw primitive (filled disc,
// line). Bound by `bindLuaDrivenEcs()` so every creation on the Lua-first
// authoring path gets them without re-declaring per-creation pass-throughs
// (the duplication this issue retires). Every binding is a thin forward to an
// existing `IRRender::` entry point — no render logic lives here.
//
// `setSunDirection` forwards to `IRRender::setSunDirection`, which preserves
// the engine-side `dir.z <= 0` assert and normalizes on write. There is no
// clear / background-color setter engine-side; `setSkyColor` (the sky
// hemisphere term) is the closest available color knob.
//
// The shape draws forward to `IRRender::drawGuiDisc` / `drawGuiLine`, which
// own the "gui"-canvas resolution and rasterization. They are IMMEDIATE-MODE:
// the GUI canvas is cleared every frame by `TEXT_TO_TRIXEL`, so a draw only
// persists on screen if it is re-issued each frame from a RENDER-phase Lua
// system positioned after that clear — the same contract the widget render
// systems follow. A one-shot draw at script-load time is wiped on the next
// frame that clears the canvas.
inline void bindRenderGlue(LuaScript &script) {
    sol::state &lua = script.lua();

    // Extend (never replace) IRRender so a creation that also adds its own
    // IRRender entries (e.g. getGuiScale, measureText) keeps them.
    if (!lua["IRRender"].valid()) {
        lua["IRRender"] = lua.create_table();
    }

    lua["IRRender"]["setSunDirection"] = [](float x, float y, float z) {
        IRRender::setSunDirection(IRMath::vec3(x, y, z));
    };
    lua["IRRender"]["setSunIntensity"] = [](float intensity) {
        IRRender::setSunIntensity(intensity);
    };
    lua["IRRender"]["setSunAmbient"] = [](float ambient) {
        IRRender::setSunAmbient(ambient);
    };
    lua["IRRender"]["setSkyColor"] = [](float r, float g, float b) {
        IRRender::setSkyColor(IRMath::vec3(r, g, b));
    };
    lua["IRRender"]["setSkyIntensity"] = [](float intensity) {
        IRRender::setSkyIntensity(intensity);
    };

    if (!lua["IRGui"].valid()) {
        lua["IRGui"] = lua.create_table();
    }

    lua["IRGui"]["drawDisc"] = [](int x, int y, int radius, sol::object color) {
        IRRender::drawGuiDisc(IRMath::ivec2(x, y), radius, colorFromLua(color));
    };

    lua["IRGui"]["drawLine"] = [](int x0, int y0, int x1, int y1, sol::object color) {
        IRRender::drawGuiLine(
            IRMath::ivec2(x0, y0),
            IRMath::ivec2(x1, y1),
            colorFromLua(color)
        );
    };
}

} // namespace IRScript::detail

#endif /* LUA_RENDER_BINDINGS_H */
