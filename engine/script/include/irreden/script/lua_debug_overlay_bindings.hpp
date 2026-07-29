#ifndef LUA_DEBUG_OVERLAY_BINDINGS_H
#define LUA_DEBUG_OVERLAY_BINDINGS_H

// IRDebug immediate-mode overlay-draw Lua bindings (engine #2375) — issue
// debug-overlay draws from an EVAL Lua system exactly like a C++ caller, so a
// Lua-driven creation no longer needs a hand-rolled C++ system just to reach
// the buffering draw calls.
//
//   IRDebug.drawLine3D(from, to, r, g, b [, a])
//   IRDebug.drawCircle3D(center, radius, r, g, b [, a [, segments]])
//   IRDebug.drawTriangle3D(a, b, c, r, g, b [, alpha])
//   IRDebug.drawDiamond3D(center, radius, r, g, b [, a])
//   IRDebug.drawPath3D(points, r, g, b [, a])        -- points: array of vec3
//   IRDebug.drawLineScreen(from, to, r, g, b [, a])
//   IRDebug.drawTriangleScreen(a, b, c, r, g, b [, alpha])
//   IRDebug.drawRectScreen(min, max, fillColor, borderColor)
//   IRDebug.drawDotScreen(center, radius, color)
//
// No GPU work crosses the Lua boundary: every binding is a thin forward to a
// pure-`push_back` buffering call in `debug_overlay_draws.hpp`. The
// `System<DEBUG_OVERLAY>` flush keeps sole ownership of the draw + the clear.
//
// IMMEDIATE MODE — the same contract as `IRGui.drawDisc`/`drawLine`: the flush
// consumes AND clears the buffers every DEBUG_OVERLAY RENDER tick, so a draw
// persists only if re-issued each frame from a RENDER-phase Lua system ordered
// BEFORE DEBUG_OVERLAY. A one-shot draw at script-load time shows for at most
// one frame. Issuing from an UPDATE-phase system is wrong in a way that isn't
// obvious: UPDATE runs on a fixed timestep, so it fires 0..N times per rendered
// frame — the overlay flickers (0 runs) or is overdrawn N times.
//
// Colors are separate 0..1 floats, mirroring the C++ surface argument-for-
// argument, NOT the 0-255 `colorFromLua` tables `IRGui` uses. Parity with the
// C++ callers wins here so existing C++ overlay code ports to Lua line-for-line.
// The two vec4-color draws (`drawRectScreen`, `drawDotScreen`) likewise take
// 0..1 float components, matching their C++ `vec4` parameters.
//
// Deliberately NOT bound: `clear()` (the flush owns clearing — a Lua caller
// clearing mid-frame would silently drop other systems' draws) and
// `worldToScreen`/`screenToWorld` (pure-math helpers; additive follow-up if a
// creation needs them).

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_math.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

#include <irreden/render/debug_overlay_draws.hpp>

#include <string>
#include <vector>

namespace IRScript::detail {

// The `*FromLua` helpers zero-default on unrecognized input by contract, so a
// typo'd argument would otherwise draw silently at the origin instead of
// failing. Validate the shape first and raise a Lua-visible error naming the
// offending argument.
inline void requireVecShape(const sol::object &obj, const char *context) {
    if (obj.is<sol::table>()) {
        return;
    }
    if (obj.is<IRMath::vec2>() || obj.is<IRMath::vec3>() || obj.is<IRMath::vec4>()) {
        return;
    }
    throw sol::error{
        std::string{context} + " must be an IRMath vector userdata or a component table"
    };
}

inline IRMath::vec2 requireVec2(const sol::object &obj, const char *context) {
    requireVecShape(obj, context);
    return vec2FromLua(obj);
}

inline IRMath::vec3 requireVec3(const sol::object &obj, const char *context) {
    requireVecShape(obj, context);
    return vec3FromLua(obj);
}

inline IRMath::vec4 requireVec4(const sol::object &obj, const char *context) {
    requireVecShape(obj, context);
    return vec4FromLua(obj);
}

inline void bindDebugOverlay(LuaScript &script) {
    sol::state &lua = script.lua();

    // Extend (never replace) IRDebug so a creation that pre-populates its own
    // IRDebug entries keeps them — same guard as IRRender / IRGui.
    if (!lua["IRDebug"].valid()) {
        lua["IRDebug"] = lua.create_table();
    }
    sol::table debug = lua["IRDebug"];

    debug["drawLine3D"] =
        [](sol::object from, sol::object to, float r, float g, float b, sol::optional<float> a) {
            IRDebug::drawLine3D(
                requireVec3(from, "IRDebug.drawLine3D: 'from'"),
                requireVec3(to, "IRDebug.drawLine3D: 'to'"),
                r,
                g,
                b,
                a.value_or(1.0f)
            );
        };

    debug["drawCircle3D"] = [](sol::object center,
                               float radius,
                               float r,
                               float g,
                               float b,
                               sol::optional<float> a,
                               sol::optional<int> segments) {
        IRDebug::drawCircle3D(
            requireVec3(center, "IRDebug.drawCircle3D: 'center'"),
            radius,
            r,
            g,
            b,
            a.value_or(1.0f),
            segments.value_or(32)
        );
    };

    debug["drawTriangle3D"] = [](sol::object a,
                                 sol::object b,
                                 sol::object c,
                                 float r,
                                 float g,
                                 float bColor,
                                 sol::optional<float> aColor) {
        IRDebug::drawTriangle3D(
            requireVec3(a, "IRDebug.drawTriangle3D: 'a'"),
            requireVec3(b, "IRDebug.drawTriangle3D: 'b'"),
            requireVec3(c, "IRDebug.drawTriangle3D: 'c'"),
            r,
            g,
            bColor,
            aColor.value_or(1.0f)
        );
    };

    debug["drawDiamond3D"] =
        [](sol::object center, float radius, float r, float g, float b, sol::optional<float> a) {
            IRDebug::drawDiamond3D(
                requireVec3(center, "IRDebug.drawDiamond3D: 'center'"),
                radius,
                r,
                g,
                b,
                a.value_or(1.0f)
            );
        };

    // `points` is an array table of vec3-shaped entries. The temp vector is
    // built per call at the boundary — acceptable for a debug surface, and
    // deliberately not cached (bindings hold no cross-frame state).
    debug["drawPath3D"] =
        [](sol::object points, float r, float g, float b, sol::optional<float> a) {
            if (!points.is<sol::table>()) {
                throw sol::error{"IRDebug.drawPath3D: 'points' must be an array table of vec3"};
            }
            sol::table pointTable = points.as<sol::table>();
            // Hoisted deliberately: `sol::table::size()` is a Lua `#` call across
            // the VM boundary, and leaving it in the loop condition re-pays it per
            // point — measured ~8.5% of this binding's cost at 200 paths/frame.
            // (Dropping the vector entirely to stream into drawLine3D was measured
            // too: only ~2% more, not worth losing all-or-nothing validation — a
            // bad entry must not leave a half-drawn path buffered.)
            const std::size_t pointCount = pointTable.size();
            std::vector<IRMath::vec3> resolved;
            resolved.reserve(pointCount);
            for (std::size_t i = 1; i <= pointCount; ++i) {
                resolved.push_back(
                    requireVec3(pointTable[i], "IRDebug.drawPath3D: every entry of 'points'")
                );
            }
            IRDebug::drawPath3D(resolved, r, g, b, a.value_or(1.0f));
        };

    debug["drawLineScreen"] =
        [](sol::object from, sol::object to, float r, float g, float b, sol::optional<float> a) {
            IRDebug::drawLineScreen(
                requireVec2(from, "IRDebug.drawLineScreen: 'from'"),
                requireVec2(to, "IRDebug.drawLineScreen: 'to'"),
                r,
                g,
                b,
                a.value_or(1.0f)
            );
        };

    debug["drawTriangleScreen"] = [](sol::object a,
                                     sol::object b,
                                     sol::object c,
                                     float r,
                                     float g,
                                     float bColor,
                                     sol::optional<float> aColor) {
        IRDebug::drawTriangleScreen(
            requireVec2(a, "IRDebug.drawTriangleScreen: 'a'"),
            requireVec2(b, "IRDebug.drawTriangleScreen: 'b'"),
            requireVec2(c, "IRDebug.drawTriangleScreen: 'c'"),
            r,
            g,
            bColor,
            aColor.value_or(1.0f)
        );
    };

    debug["drawRectScreen"] =
        [](sol::object min, sol::object max, sol::object fillColor, sol::object borderColor) {
            IRDebug::drawRectScreen(
                requireVec2(min, "IRDebug.drawRectScreen: 'min'"),
                requireVec2(max, "IRDebug.drawRectScreen: 'max'"),
                requireVec4(fillColor, "IRDebug.drawRectScreen: 'fillColor'"),
                requireVec4(borderColor, "IRDebug.drawRectScreen: 'borderColor'")
            );
        };

    debug["drawDotScreen"] = [](sol::object center, float radius, sol::object color) {
        IRDebug::drawDotScreen(
            requireVec2(center, "IRDebug.drawDotScreen: 'center'"),
            radius,
            requireVec4(color, "IRDebug.drawDotScreen: 'color'")
        );
    };
}

} // namespace IRScript::detail

#endif /* LUA_DEBUG_OVERLAY_BINDINGS_H */
