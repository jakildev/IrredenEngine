#ifndef COMPONENT_TRIANGLE_CANVAS_BACKGROUND_LUA_H
#define COMPONENT_TRIANGLE_CANVAS_BACKGROUND_LUA_H

#include <irreden/render/components/component_triangle_canvas_background.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::BackgroundTypes> = true;

template <> inline void bindLuaType<IRComponents::BackgroundTypes>(LuaScript &luaScript) {
    luaScript.registerEnum<IRComponents::BackgroundTypes>(
        "BackgroundTypes",
        {{"SINGLE_COLOR", IRComponents::BackgroundTypes::kSingleColor},
         {"GRADIENT", IRComponents::BackgroundTypes::kGradient},
         {"GRADIENT_RANDOM", IRComponents::BackgroundTypes::kGradientRandom},
         {"PULSE_PATTERN", IRComponents::BackgroundTypes::kPulsePattern}}
    );
}

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_TriangleCanvasBackground> = true;

template <>
inline void bindLuaType<IRComponents::C_TriangleCanvasBackground>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_TriangleCanvasBackground,
        IRComponents::C_TriangleCanvasBackground(
            IRComponents::BackgroundTypes,
            IRMath::Color,
            IRMath::Color,
            IRMath::ivec2,
            float,
            int
        ),
        IRComponents::C_TriangleCanvasBackground()>(
        "C_TriangleCanvasBackground",
        "zoomPatternIn",
        &IRComponents::C_TriangleCanvasBackground::zoomPatternIn,
        "zoomPatternOut",
        &IRComponents::C_TriangleCanvasBackground::zoomPatternOut,
        "setPatternZoomMultiplier",
        &IRComponents::C_TriangleCanvasBackground::setPatternZoomMultiplier,
        "setPulseWaveDirection",
        &IRComponents::C_TriangleCanvasBackground::setPulseWaveDirection,
        "setPulseWaveDirectionLinearMotion",
        &IRComponents::C_TriangleCanvasBackground::setPulseWaveDirectionLinearMotion,
        "clearPulseWaveDirectionLinearMotion",
        &IRComponents::C_TriangleCanvasBackground::clearPulseWaveDirectionLinearMotion,
        "setPulseWavePrimaryTiming",
        &IRComponents::C_TriangleCanvasBackground::setPulseWavePrimaryTiming,
        "setPulseWaveInterference",
        &IRComponents::C_TriangleCanvasBackground::setPulseWaveInterference,
        "setPulseWaveSecondaryDirectionLinearMotion",
        &IRComponents::C_TriangleCanvasBackground::setPulseWaveSecondaryDirectionLinearMotion,
        "clearPulseWaveSecondaryDirectionLinearMotion",
        &IRComponents::C_TriangleCanvasBackground::clearPulseWaveSecondaryDirectionLinearMotion,
        "setPulseWaveSecondaryTiming",
        &IRComponents::C_TriangleCanvasBackground::setPulseWaveSecondaryTiming
    );
}
} // namespace IRScript

#endif /* COMPONENT_TRIANGLE_CANVAS_BACKGROUND_LUA_H */
