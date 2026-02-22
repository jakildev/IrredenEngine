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
#include <irreden/render/components/component_triangle_canvas_background.hpp>

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
        luaScript.lua()["IRMath"]["lerpColor"] = [](const Color &a, const Color &b, float t) {
            return IRMath::lerpColor(a, b, t);
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

        registerLuaComponentPack(luaScript);
        luaScript.registerType<IRScript::LuaEntity, IRScript::LuaEntity(EntityId)>(
            "LuaEntity",
            "entity",
            [](IRScript::LuaEntity &obj) { return obj.entity; }
        );

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

        // Batch creation: static note platforms with spring visual behavior.
        luaScript.registerCreateEntityBatchFunction<
            C_Position3D,
            C_VoxelSetNew,
            C_ColliderIso3DAABB,
            C_CollisionLayer,
            C_ContactEvent,
            C_Velocity3D,
            C_ReactiveReturn3D>("createEntityBatchNotePlatforms");

        // Direct entity creation for MIDI sequences
        luaScript.registerCreateEntityFunction<C_MidiSequence>("createMidiSequence");

        // Audio port management
        luaScript.lua()["IRAudio"] = luaScript.lua().create_table();
        luaScript.lua()["IRAudio"]["openMidiOut"] = [](const std::string &name) {
            return IRAudio::openPortMidiOut(name);
        };
        luaScript.lua()["IRAudio"]["openMidiIn"] = [](const std::string &name) {
            return IRAudio::openPortMidiIn(name);
        };

        luaScript.lua()["IRVisual"] = luaScript.lua().create_table();
        luaScript.lua()["IRVisual"]["setMainCanvasPulseBackground"] =
            [](const Color &colorA, const Color &colorB, float pulseSpeed, int patternScale) {
                IREntity::EntityId backgroundCanvas = IRRender::getCanvas("background");
                ivec2 size = IREntity::getComponent<C_SizeTriangles>(backgroundCanvas).size_;
                IREntity::setComponent(
                    backgroundCanvas,
                    C_TriangleCanvasBackground{
                        BackgroundTypes::kPulsePattern,
                        {colorA, colorB},
                        size,
                        pulseSpeed,
                        patternScale
                    }
                );
            };
        luaScript.lua()["IRVisual"]["clearMainCanvasBackground"] = []() {
            IREntity::EntityId backgroundCanvas = IRRender::getCanvas("background");
            ivec2 size = IREntity::getComponent<C_SizeTriangles>(backgroundCanvas).size_;
            IREntity::setComponent(
                backgroundCanvas,
                C_TriangleCanvasBackground{
                    BackgroundTypes::kSingleColor,
                    {IRColors::kInvisable},
                    size
                }
            );
        };

        luaScript.lua()["IRCollisionLayer"] = luaScript.lua().create_table();
        luaScript.lua()["IRCollisionLayer"]["NOTE_BLOCK"] = COLLISION_LAYER_NOTE_BLOCK;
        luaScript.lua()["IRCollisionLayer"]["NOTE_PLATFORM"] = COLLISION_LAYER_NOTE_PLATFORM;
        luaScript.lua()["IRCollisionLayer"]["PARTICLE"] = COLLISION_LAYER_PARTICLE;
        luaScript.lua()["IRCollisionLayer"]["DEFAULT"] = COLLISION_LAYER_DEFAULT;
    });

    isRegistered = true;
}
} // namespace MidiPolyrhythm
