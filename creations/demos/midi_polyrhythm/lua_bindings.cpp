#include "lua_bindings.hpp"
#include "lua_component_pack.hpp"

#include <irreden/ir_engine.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_audio.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/audio/components/component_midi_channel.hpp>
#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/render/components/component_triangle_canvas_background.hpp>
#include <irreden/update/systems/system_gravity.hpp>
#include <irreden/update/components/component_action_animation.hpp>
#include <irreden/update/components/component_animation_clip.hpp>
#include <irreden/update/components/component_anim_clip_color_track.hpp>
#include <irreden/update/components/component_anim_color_state.hpp>
#include <irreden/update/components/component_anim_motion_color_shift.hpp>

namespace MidiPolyrhythm {
void registerLuaBindings() {
    static bool isRegistered = false;
    if (isRegistered) {
        return;
    }

    IREngine::registerLuaBindings([](IRScript::LuaScript &luaScript) {
        using namespace IRMath;
        using namespace IRComponents;
        using namespace IRConstants;
        using namespace IRAudio;

        luaScript.registerType<Color, Color(int, int, int, int)>(
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
        luaScript.lua().new_usertype<ColorHSV>(
            "ColorHSV",
            sol::call_constructor, sol::factories(
                [](float h, float s, float v, float a) -> ColorHSV {
                    return ColorHSV{h, s, v, a};
                },
                []() -> ColorHSV {
                    return ColorHSV{0.0f, 0.0f, 0.0f, 1.0f};
                }
            ),
            "h", &ColorHSV::hue_,
            "s", &ColorHSV::saturation_,
            "v", &ColorHSV::value_,
            "a", &ColorHSV::alpha_
        );
        luaScript.lua()["IRMath"]["lerpColor"] = [](const Color &a, const Color &b, float t) {
            return IRMath::lerpColor(a, b, t);
        };

        auto luaTableToColors = [](const sol::table &t) {
            std::vector<Color> out;
            out.reserve(t.size());
            for (size_t i = 1; i <= t.size(); ++i)
                out.push_back(t[i].get<Color>());
            return out;
        };
        auto colorsToLuaTable = [](const std::vector<Color> &v, sol::this_state L) {
            sol::state_view lua(L);
            sol::table out = lua.create_table(static_cast<int>(v.size()), 0);
            for (size_t i = 0; i < v.size(); ++i)
                out[i + 1] = v[i];
            return out;
        };
        luaScript.lua()["IRMath"]["sortByHue"] =
            [=](const sol::table &t, sol::this_state L) {
                return colorsToLuaTable(IRMath::sortByHue(luaTableToColors(t)), L);
            };
        luaScript.lua()["IRMath"]["sortBySaturation"] =
            [=](const sol::table &t, sol::this_state L) {
                return colorsToLuaTable(IRMath::sortBySaturation(luaTableToColors(t)), L);
            };
        luaScript.lua()["IRMath"]["sortByValue"] =
            [=](const sol::table &t, sol::this_state L) {
                return colorsToLuaTable(IRMath::sortByValue(luaTableToColors(t)), L);
            };
        luaScript.lua()["IRMath"]["sortByLuminance"] =
            [=](const sol::table &t, sol::this_state L) {
                return colorsToLuaTable(IRMath::sortByLuminance(luaTableToColors(t)), L);
            };
        luaScript.lua()["IRMath"]["colorToHSV"] = [](const Color &c) {
            return IRMath::colorToColorHSV(c);
        };
        luaScript.lua()["IRMath"]["hsvToColor"] = [](const ColorHSV &hsv) {
            return IRMath::colorHSVToColor(hsv);
        };
        luaScript.lua()["IRMath"]["isoDepthShift"] =
            [](const vec3 &pos, float depth) {
                return IRMath::isoDepthShift(pos, depth);
            };
        luaScript.lua()["IRMath"]["loadPalette"] =
            [](const std::string &filename, sol::this_state L) {
                auto colors = IRRender::createColorPaletteFromFile(filename.c_str());
                sol::state_view lua(L);
                sol::table result =
                    lua.create_table(static_cast<int>(colors.size()), 0);
                for (size_t i = 0; i < colors.size(); ++i) {
                    result[i + 1] = colors[i];
                }
                return result;
            };
        luaScript.registerType<ivec3, ivec3(int, int, int)>(
            "ivec3",
            "x",
            &ivec3::x,
            "y",
            &ivec3::y,
            "z",
            &ivec3::z
        );
        luaScript.registerType<ivec2, ivec2(int, int)>(
            "ivec2",
            "x",
            &ivec2::x,
            "y",
            &ivec2::y
        );
        luaScript.registerType<vec2, vec2(float, float)>(
            "vec2",
            "x",
            &vec2::x,
            "y",
            &vec2::y
        );
        auto vec3Type = luaScript.registerType<vec3, vec3(float, float, float)>(
            "vec3",
            "x",
            &vec3::x,
            "y",
            &vec3::y,
            "z",
            &vec3::z
        );
        vec3Type[sol::meta_function::addition] = [](const vec3 &a, const vec3 &b) { return a + b; };
        vec3Type[sol::meta_function::subtraction] = [](const vec3 &a, const vec3 &b) {
            return a - b;
        };
        vec3Type[sol::meta_function::multiplication] = sol::overload(
            [](const vec3 &a, float b) { return a * b; },
            [](float a, const vec3 &b) { return a * b; }
        );

        luaScript.registerEnum<IREasingFunctions>(
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

        luaScript.registerEnum<IRMidiNote>(
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
             {"C6", NOTE_C6},        {"D6", NOTE_D6},        {"E6", NOTE_E6},
             {"F6", NOTE_F6},        {"G6", NOTE_G6},        {"A6", NOTE_A6},
             {"B6", NOTE_B6},        {"C7", NOTE_C7},        {"C8", NOTE_C8}}
        );

        luaScript.registerEnum<IRNoteName>(
            "NoteName",
            {{"C",  NOTE_NAME_C},       {"Cs", NOTE_NAME_C_SHARP},
             {"Db", NOTE_NAME_D_FLAT},   {"D",  NOTE_NAME_D},
             {"Ds", NOTE_NAME_D_SHARP},  {"Eb", NOTE_NAME_E_FLAT},
             {"E",  NOTE_NAME_E},        {"F",  NOTE_NAME_F},
             {"Fs", NOTE_NAME_F_SHARP},  {"Gb", NOTE_NAME_G_FLAT},
             {"G",  NOTE_NAME_G},        {"Gs", NOTE_NAME_G_SHARP},
             {"Ab", NOTE_NAME_A_FLAT},   {"A",  NOTE_NAME_A},
             {"As", NOTE_NAME_A_SHARP},  {"Bb", NOTE_NAME_B_FLAT},
             {"B",  NOTE_NAME_B}}
        );

        luaScript.registerEnum<IRScaleMode>(
            "ScaleMode",
            {// Diatonic modes
             {"IONIAN",             SCALE_IONIAN},
             {"MAJOR",              SCALE_MAJOR},
             {"DORIAN",             SCALE_DORIAN},
             {"PHRYGIAN",           SCALE_PHRYGIAN},
             {"LYDIAN",             SCALE_LYDIAN},
             {"MIXOLYDIAN",         SCALE_MIXOLYDIAN},
             {"AEOLIAN",            SCALE_AEOLIAN},
             {"MINOR",              SCALE_MINOR},
             {"LOCRIAN",            SCALE_LOCRIAN},
             // Harmonic / melodic variants
             {"HARMONIC_MINOR",     SCALE_HARMONIC_MINOR},
             {"MELODIC_MINOR",      SCALE_MELODIC_MINOR},
             {"HUNGARIAN_MINOR",    SCALE_HUNGARIAN_MINOR},
             {"DOUBLE_HARMONIC",    SCALE_DOUBLE_HARMONIC},
             {"NEAPOLITAN_MINOR",   SCALE_NEAPOLITAN_MINOR},
             {"NEAPOLITAN_MAJOR",   SCALE_NEAPOLITAN_MAJOR},
             {"ENIGMATIC",          SCALE_ENIGMATIC},
             {"PERSIAN",            SCALE_PERSIAN},
             // Pentatonic
             {"PENTATONIC_MAJOR",   SCALE_PENTATONIC_MAJOR},
             {"PENTATONIC_MINOR",   SCALE_PENTATONIC_MINOR},
             {"HIRAJOSHI",          SCALE_HIRAJOSHI},
             {"IN_SEN",             SCALE_IN_SEN},
             {"IWATO",              SCALE_IWATO},
             {"PELOG",              SCALE_PELOG},
             // Hexatonic
             {"WHOLE_TONE",         SCALE_WHOLE_TONE},
             {"BLUES",              SCALE_BLUES},
             {"AUGMENTED",          SCALE_AUGMENTED},
             {"PROMETHEUS",         SCALE_PROMETHEUS},
             {"TRITONE",            SCALE_TRITONE},
             // Octatonic
             {"DIMINISHED_WHOLE_HALF", SCALE_DIMINISHED_WHOLE_HALF},
             {"DIMINISHED_HALF_WHOLE", SCALE_DIMINISHED_HALF_WHOLE},
             {"BEBOP_DOMINANT",     SCALE_BEBOP_DOMINANT},
             {"BEBOP_MAJOR",        SCALE_BEBOP_MAJOR},
             // Chromatic
             {"CHROMATIC",          SCALE_CHROMATIC}}
        );

        luaScript.registerEnum<IRComponents::ActionAnimationTriggerMode>(
            "AnimTriggerMode",
            {{"CONTACT_ENTER", IRComponents::ANIM_TRIGGER_CONTACT_ENTER},
             {"TIMER_SYNC",    IRComponents::ANIM_TRIGGER_TIMER_SYNC},
             {"KEYPRESS",      IRComponents::ANIM_TRIGGER_KEYPRESS},
             {"MANUAL",        IRComponents::ANIM_TRIGGER_MANUAL}}
        );

        luaScript.registerEnum<IRComponents::AnimColorBlendMode>(
            "AnimColorBlendMode",
            {{"REPLACE",  IRComponents::ANIM_COLOR_BLEND_REPLACE},
             {"MULTIPLY", IRComponents::ANIM_COLOR_BLEND_MULTIPLY},
             {"LERP",     IRComponents::ANIM_COLOR_BLEND_LERP}}
        );

        luaScript.registerEnum<IRComponents::AnimColorTrackMode>(
            "AnimColorTrackMode",
            {{"ABSOLUTE",   IRComponents::ANIM_COLOR_TRACK_ABSOLUTE},
             {"HSV_OFFSET", IRComponents::ANIM_COLOR_TRACK_HSV_OFFSET},
             {"HSV_OFFSET_STATE_BLEND",
              IRComponents::ANIM_COLOR_TRACK_HSV_OFFSET_STATE_BLEND},
             {"HSV_OFFSET_TIMELINE",
              IRComponents::ANIM_COLOR_TRACK_HSV_OFFSET_TIMELINE}}
        );

        luaScript.registerType<C_HasGravity, C_HasGravity()>("C_HasGravity");
        registerLuaComponentPack(luaScript);
        luaScript.registerType<IRScript::LuaEntity, IRScript::LuaEntity(EntityId)>(
            "LuaEntity",
            "entity",
            [](IRScript::LuaEntity &obj) { return obj.entity; }
        );

        luaScript.lua()["IRRender"] = luaScript.lua().create_table();
        luaScript.lua()["IRRender"]["setCameraZoom"] = [](float zoom) {
            IRRender::setCameraZoom(zoom);
        };

        luaScript.lua()["IREntity"] = luaScript.lua().create_table();
        luaScript.registerType<
            IREntity::CreateEntityCallbackParams,
            IREntity::CreateEntityCallbackParams(ivec3, vec3)>(
            "CreateEntityCallbackParams",
            "center",
            &IREntity::CreateEntityCallbackParams::center,
            "index",
            &IREntity::CreateEntityCallbackParams::index
        );
        luaScript.lua()["IREntity"]["getEntity"] = [](const std::string &entityName) {
            return IRScript::LuaEntity{IREntity::getEntity(entityName)};
        };
        luaScript.lua()["IREntity"]["getCanvasEntity"] = [](const std::string &canvasName) {
            return IRScript::LuaEntity{IRRender::getCanvas(canvasName)};
        };
        luaScript.lua()["IREntity"]["getCanvasSizeTriangles"] = [](IRScript::LuaEntity entity) {
            return IREntity::getComponent<C_SizeTriangles>(entity.entity).size_;
        };
        luaScript.lua()["IREntity"]["setTriangleCanvasBackground"] =
            [](IRScript::LuaEntity entity, const C_TriangleCanvasBackground &background) {
                IREntity::setComponent(entity.entity, background);
            };
        luaScript.lua()["IREntity"]["clearTriangleCanvasBackground"] = [](IRScript::LuaEntity entity
                                                                        ) {
            IREntity::removeComponent<C_TriangleCanvasBackground>(entity.entity);
        };
        luaScript.lua()["IREntity"]["setTrixelCanvasRenderBehavior"] =
            [](IRScript::LuaEntity entity, const C_TrixelCanvasRenderBehavior &behavior) {
                IREntity::setComponent(entity.entity, behavior);
            };
        luaScript.lua()["IREntity"]["setZoomLevel"] =
            [](IRScript::LuaEntity entity, const C_ZoomLevel &zoomLevel) {
                IREntity::setComponent(entity.entity, zoomLevel);
            };

        // Batch creation: visual-only oscillating voxels
        luaScript.registerCreateEntityBatchFunction<C_Position3D, C_VoxelSetNew, C_PeriodicIdle>(
            "createEntityBatchVoxelPeriodicIdle"
        );

        // Batch creation: polyrhythmic voices (visual + MIDI trigger)
        luaScript.registerCreateEntityBatchFunction<
            C_Position3D,
            C_VoxelSetNew,
            C_PeriodicIdle,
            C_MidiNote>("createEntityBatchPolyrhythm");

        // Batch creation: polyrhythmic voices with particle bursts
        luaScript.registerCreateEntityBatchFunction<
            C_Position3D,
            C_VoxelSetNew,
            C_PeriodicIdle,
            C_MidiNote,
            C_ParticleBurst>("createEntityBatchPolyrhythmBurst");

        // Batch creation: collision-driven note blocks.
        luaScript.registerCreateEntityBatchFunction<
            C_Position3D,
            C_VoxelSetNew,
            C_PeriodicIdle,
            C_ColliderIso3DAABB,
            C_CollisionLayer,
            C_ContactEvent,
            C_MidiNote,
            C_ParticleBurst,
            C_TriggerGlow>("createEntityBatchNoteBlocks");

        // Batch creation: physics-driven note blocks (gravity + rhythmic launch).
        luaScript.registerCreateEntityBatchFunction<
            C_Position3D,
            C_VoxelSetNew,
            C_Velocity3D,
            C_HasGravity,
            C_RhythmicLaunch,
            C_ColliderIso3DAABB,
            C_CollisionLayer,
            C_ContactEvent,
            C_MidiNote,
            C_ParticleBurst,
            C_TriggerGlow>("createEntityBatchNoteBlocksPhysics");

        // Batch creation: static note platforms with spring visual behavior.
        luaScript.registerCreateEntityBatchFunction<
            C_Position3D,
            C_VoxelSetNew,
            C_ColliderIso3DAABB,
            C_CollisionLayer,
            C_ContactEvent,
            C_Velocity3D,
            C_ReactiveReturn3D,
            C_TriggerGlow>("createEntityBatchNotePlatforms");

        // Batch creation: note platforms with action animation.
        luaScript.registerCreateEntityBatchFunction<
            C_Position3D,
            C_VoxelSetNew,
            C_ColliderIso3DAABB,
            C_CollisionLayer,
            C_ContactEvent,
            C_Velocity3D,
            C_ReactiveReturn3D,
            C_TriggerGlow,
            C_ActionAnimation>("createEntityBatchNotePlatformsAnimated");

        // Direct entity creation for MIDI sequences
        luaScript.registerCreateEntityFunction<C_MidiSequence>("createMidiSequence");

        // Direct entity creation for shared animation clips
        luaScript.registerCreateEntityFunction<C_AnimationClip>("createAnimationClip");

        // Animation clip with color track (both components on same entity)
        luaScript.registerCreateEntityFunction<
            C_AnimationClip, C_AnimClipColorTrack>("createAnimationClipWithColor");

        // Batch creation: note platforms with animation + color modifiers.
        luaScript.registerCreateEntityBatchFunction<
            C_Position3D,
            C_VoxelSetNew,
            C_ColliderIso3DAABB,
            C_CollisionLayer,
            C_ContactEvent,
            C_Velocity3D,
            C_ReactiveReturn3D,
            C_TriggerGlow,
            C_ActionAnimation,
            C_AnimColorState,
            C_AnimMotionColorShift>("createEntityBatchNotePlatformsAnimatedColor");

        // Audio port management
        luaScript.lua()["IRAudio"] = luaScript.lua().create_table();
        luaScript.lua()["IRAudio"]["openMidiOut"] = [](const std::string &name) {
            return IRAudio::openPortMidiOut(name);
        };
        luaScript.lua()["IRAudio"]["openMidiIn"] = [](const std::string &name) {
            return IRAudio::openPortMidiIn(name);
        };

        luaScript.lua()["IRAudio"]["rootNote"] = [](int noteName, int octave) {
            return IRAudio::rootMidiNote(noteName, octave);
        };

        luaScript.lua()["IRAudio"]["getScaleIntervals"] =
            [](int mode, sol::this_state L) {
                sol::state_view lua(L);
                const auto &def = IRAudio::kScaleDefinitions[mode];
                sol::table t = lua.create_table(def.size, 0);
                for (int i = 0; i < def.size; ++i) {
                    t[i + 1] = def.intervals[i];
                }
                return t;
            };

        luaScript.lua()["IRAudio"]["getScaleSize"] = [](int mode) {
            return IRAudio::kScaleDefinitions[mode].size;
        };

        luaScript.lua()["IRCollisionLayer"] = luaScript.lua().create_table();
        luaScript.lua()["IRCollisionLayer"]["NOTE_BLOCK"] = COLLISION_LAYER_NOTE_BLOCK;
        luaScript.lua()["IRCollisionLayer"]["NOTE_PLATFORM"] = COLLISION_LAYER_NOTE_PLATFORM;
        luaScript.lua()["IRCollisionLayer"]["PARTICLE"] = COLLISION_LAYER_PARTICLE;
        luaScript.lua()["IRCollisionLayer"]["DEFAULT"] = COLLISION_LAYER_DEFAULT;

        luaScript.lua()["IRPhysics"] = luaScript.lua().create_table();
        luaScript.lua()["IRPhysics"]["setGravityMagnitude"] = [](float magnitude) {
            IRSystem::System<IRSystem::GRAVITY_3D>::gravity().setMagnitude(magnitude);
        };
        luaScript.lua()["IRPhysics"]["getGravityMagnitude"] = []() -> float {
            return IRSystem::System<IRSystem::GRAVITY_3D>::gravity().magnitude_.magnitude_;
        };
        luaScript.lua()["IRPhysics"]["impulseForHeight"] = [](float gravity, float height) {
            return IRMath::impulseForHeight(gravity, height);
        };
        luaScript.lua()["IRPhysics"]["flightTimeForHeight"] = [](float gravity, float height) {
            return IRMath::flightTimeForHeight(gravity, height);
        };
    });

    isRegistered = true;
}
} // namespace MidiPolyrhythm
