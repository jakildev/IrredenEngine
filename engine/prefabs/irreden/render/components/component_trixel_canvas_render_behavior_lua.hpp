#ifndef COMPONENT_TRIXEL_CANVAS_RENDER_BEHAVIOR_LUA_H
#define COMPONENT_TRIXEL_CANVAS_RENDER_BEHAVIOR_LUA_H

#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_TrixelCanvasRenderBehavior> = true;

template <>
inline void bindLuaType<IRComponents::C_TrixelCanvasRenderBehavior>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_TrixelCanvasRenderBehavior,
        IRComponents::C_TrixelCanvasRenderBehavior(
            bool,
            bool,
            bool,
            bool,
            bool,
            float,
            float,
            float,
            float
        ),
        IRComponents::C_TrixelCanvasRenderBehavior()>(
        "C_TrixelCanvasRenderBehavior",
        "useCameraPositionIso",
        &IRComponents::C_TrixelCanvasRenderBehavior::useCameraPositionIso_,
        "useCameraZoom",
        &IRComponents::C_TrixelCanvasRenderBehavior::useCameraZoom_,
        "applyRenderSubdivisions",
        &IRComponents::C_TrixelCanvasRenderBehavior::applyRenderSubdivisions_,
        "mouseHoverEnabled",
        &IRComponents::C_TrixelCanvasRenderBehavior::mouseHoverEnabled_,
        "usePixelPerfectCameraOffset",
        &IRComponents::C_TrixelCanvasRenderBehavior::usePixelPerfectCameraOffset_,
        "parityOffsetIsoX",
        &IRComponents::C_TrixelCanvasRenderBehavior::parityOffsetIsoX_,
        "parityOffsetIsoY",
        &IRComponents::C_TrixelCanvasRenderBehavior::parityOffsetIsoY_,
        "staticPixelOffsetX",
        &IRComponents::C_TrixelCanvasRenderBehavior::staticPixelOffsetX_,
        "staticPixelOffsetY",
        &IRComponents::C_TrixelCanvasRenderBehavior::staticPixelOffsetY_
    );
}
} // namespace IRScript

#endif /* COMPONENT_TRIXEL_CANVAS_RENDER_BEHAVIOR_LUA_H */
