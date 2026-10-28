#ifndef LUA_BINDINGS_DEFAULT_H
#define LUA_BINDINGS_DEFAULT_H

#include <irreden/audio/ir_audio_types.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

/// Registers the math usertypes, enums and text-align tables every Lua
/// creation needs before it can spawn anything: `Color`, `vec2`, `vec3`
/// (with `+` / `-`), `vec4`, `ivec2`, `ivec3`, `IREasingFunction`, `MidiNote`,
/// `TextAlignH` and `TextAlignV`. Call it first in a creation's binding setup.
/// Idempotent per `LuaScript`: a repeat call returns before touching the state.
inline void registerStandardBindings(LuaScript &script) {
    using namespace IRMath;
    using namespace IRAudio;
    using IRComponents::TextAlignH;
    using IRComponents::TextAlignV;

    constexpr const char *kRegisteredKey = "IRScript.standardBindingsRegistered";
    sol::table registry = script.lua().registry();
    if (registry[kRegisteredKey].valid()) {
        return;
    }
    registry[kRegisteredKey] = true;

    script.registerType<Color, Color(int, int, int, int)>(
        "Color",
        "r",
        &Color::red_,
        "g",
        &Color::green_,
        "b",
        &Color::blue_,
        "a",
        &Color::alpha_
    );
    script.registerType<vec2, vec2(float, float)>("vec2", "x", &vec2::x, "y", &vec2::y);
    auto vec3Type = script.registerType<vec3, vec3(float, float, float)>(
        "vec3",
        "x",
        &vec3::x,
        "y",
        &vec3::y,
        "z",
        &vec3::z
    );
    vec3Type[sol::meta_function::addition] = [](const vec3 &a, const vec3 &b) { return a + b; };
    vec3Type[sol::meta_function::subtraction] = [](const vec3 &a, const vec3 &b) { return a - b; };
    script.registerType<vec4, vec4(float, float, float, float)>(
        "vec4",
        "x",
        &vec4::x,
        "y",
        &vec4::y,
        "z",
        &vec4::z,
        "w",
        &vec4::w
    );
    script.registerType<ivec2, ivec2(int, int)>("ivec2", "x", &ivec2::x, "y", &ivec2::y);
    script.registerType<ivec3, ivec3(int, int, int)>(
        "ivec3",
        "x",
        &ivec3::x,
        "y",
        &ivec3::y,
        "z",
        &ivec3::z
    );

    script.registerEnum<IREasingFunctions>(
        "IREasingFunction",
        {{"LINEAR_INTERPOLATION", kLinearInterpolation},
         {"QUADRATIC_EASE_IN", kQuadraticEaseIn},
         {"QUADRATIC_EASE_OUT", kQuadraticEaseOut},
         {"QUADRATIC_EASE_IN_OUT", kQuadraticEaseInOut},
         {"CUBIC_EASE_IN", kCubicEaseIn},
         {"CUBIC_EASE_OUT", kCubicEaseOut},
         {"CUBIC_EASE_IN_OUT", kCubicEaseInOut},
         {"QUARTIC_EASE_IN", kQuarticEaseIn},
         {"QUARTIC_EASE_OUT", kQuarticEaseOut},
         {"QUARTIC_EASE_IN_OUT", kQuarticEaseInOut},
         {"QUINTIC_EASE_IN", kQuinticEaseIn},
         {"QUINTIC_EASE_OUT", kQuinticEaseOut},
         {"QUINTIC_EASE_IN_OUT", kQuinticEaseInOut},
         {"SINE_EASE_IN", kSineEaseIn},
         {"SINE_EASE_OUT", kSineEaseOut},
         {"SINE_EASE_IN_OUT", kSineEaseInOut},
         {"CIRCULAR_EASE_IN", kCircularEaseIn},
         {"CIRCULAR_EASE_OUT", kCircularEaseOut},
         {"CIRCULAR_EASE_IN_OUT", kCircularEaseInOut},
         {"EXPONENTIAL_EASE_IN", kExponentialEaseIn},
         {"EXPONENTIAL_EASE_OUT", kExponentialEaseOut},
         {"EXPONENTIAL_EASE_IN_OUT", kExponentialEaseInOut},
         {"ELASTIC_EASE_IN", kElasticEaseIn},
         {"ELASTIC_EASE_OUT", kElasticEaseOut},
         {"ELASTIC_EASE_IN_OUT", kElasticEaseInOut},
         {"BACK_EASE_IN", kBackEaseIn},
         {"BACK_EASE_OUT", kBackEaseOut},
         {"BACK_EASE_IN_OUT", kBackEaseInOut},
         {"BOUNCE_EASE_IN", kBounceEaseIn},
         {"BOUNCE_EASE_OUT", kBounceEaseOut},
         {"BOUNCE_EASE_IN_OUT", kBounceEaseInOut}}
    );

    script.registerEnum<IRMidiNote>(
        "MidiNote",
        {{"A0", NOTE_A0},        {"Bb0", NOTE_A0_SHARP}, {"B0", NOTE_B0},
         {"C1", NOTE_C1},        {"Db1", NOTE_C1_SHARP}, {"D1", NOTE_D1},
         {"Eb1", NOTE_D1_SHARP}, {"E1", NOTE_E1},        {"F1", NOTE_F1},
         {"Gb1", NOTE_F1_SHARP}, {"G1", NOTE_G1},        {"Ab1", NOTE_G1_SHARP},
         {"A1", NOTE_A1},        {"Bb1", NOTE_A1_SHARP}, {"B1", NOTE_B1},
         {"C2", NOTE_C2},        {"Db2", NOTE_C2_SHARP}, {"D2", NOTE_D2},
         {"Eb2", NOTE_D2_SHARP}, {"E2", NOTE_E2},        {"F2", NOTE_F2},
         {"Gb2", NOTE_F2_SHARP}, {"G2", NOTE_G2},        {"Ab2", NOTE_G2_SHARP},
         {"A2", NOTE_A2},        {"Bb2", NOTE_A2_SHARP}, {"B2", NOTE_B2},
         {"C3", NOTE_C3},        {"Db3", NOTE_C3_SHARP}, {"D3", NOTE_D3},
         {"Eb3", NOTE_D3_SHARP}, {"E3", NOTE_E3},        {"F3", NOTE_F3},
         {"Gb3", NOTE_F3_SHARP}, {"G3", NOTE_G3},        {"Ab3", NOTE_G3_SHARP},
         {"A3", NOTE_A3},        {"Bb3", NOTE_A3_SHARP}, {"B3", NOTE_B3},
         {"C4", NOTE_C4},        {"Db4", NOTE_C4_SHARP}, {"D4", NOTE_D4},
         {"Eb4", NOTE_D4_SHARP}, {"E4", NOTE_E4},        {"F4", NOTE_F4},
         {"Gb4", NOTE_F4_SHARP}, {"G4", NOTE_G4},        {"Ab4", NOTE_G4_SHARP},
         {"A4", NOTE_A4},        {"Bb4", NOTE_A4_SHARP}, {"B4", NOTE_B4},
         {"C5", NOTE_C5},        {"Db5", NOTE_C5_SHARP}, {"D5", NOTE_D5},
         {"Eb5", NOTE_D5_SHARP}, {"E5", NOTE_E5},        {"F5", NOTE_F5},
         {"Gb5", NOTE_F5_SHARP}, {"G5", NOTE_G5},        {"Ab5", NOTE_G5_SHARP},
         {"A5", NOTE_A5},        {"Bb5", NOTE_A5_SHARP}, {"B5", NOTE_B5},
         {"C6", NOTE_C6},        {"Db6", NOTE_C6_SHARP}, {"D6", NOTE_D6},
         {"Eb6", NOTE_D6_SHARP}, {"E6", NOTE_E6},        {"F6", NOTE_F6},
         {"Gb6", NOTE_F6_SHARP}, {"G6", NOTE_G6},        {"Ab6", NOTE_G6_SHARP},
         {"A6", NOTE_A6},        {"Bb6", NOTE_A6_SHARP}, {"B6", NOTE_B6},
         {"C7", NOTE_C7},        {"Db7", NOTE_C7_SHARP}, {"D7", NOTE_D7},
         {"Eb7", NOTE_D7_SHARP}, {"E7", NOTE_E7},        {"F7", NOTE_F7},
         {"Gb7", NOTE_F7_SHARP}, {"G7", NOTE_G7},        {"Ab7", NOTE_G7_SHARP},
         {"A7", NOTE_A7},        {"Bb7", NOTE_A7_SHARP}, {"B7", NOTE_B7},
         {"C8", NOTE_C8}}
    );

    script.lua()["TextAlignH"] = script.lua().create_table_with(
        "LEFT",
        static_cast<int>(TextAlignH::LEFT),
        "CENTER",
        static_cast<int>(TextAlignH::CENTER),
        "RIGHT",
        static_cast<int>(TextAlignH::RIGHT)
    );
    script.lua()["TextAlignV"] = script.lua().create_table_with(
        "TOP",
        static_cast<int>(TextAlignV::TOP),
        "CENTER",
        static_cast<int>(TextAlignV::CENTER),
        "BOTTOM",
        static_cast<int>(TextAlignV::BOTTOM)
    );
}

} // namespace IRScript

#endif /* LUA_BINDINGS_DEFAULT_H */
