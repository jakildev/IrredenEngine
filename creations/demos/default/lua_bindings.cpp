#include "lua_bindings.hpp"
#include "lua_component_pack.hpp"

#include <irreden/ir_engine.hpp>
#include <irreden/ir_constants.hpp>

namespace IRDefaultCreation {
void registerLuaBindings() {
    static bool isRegistered = false;
    if (isRegistered) {
        return;
    }

    IREngine::registerLuaBindings([](IRScript::LuaScript &luaScript) {
        using namespace IRMath;
        using namespace IRComponents;
        using namespace IRConstants;

        luaScript.registerType<Color, Color(int, int, int, int)>("Color", "r", &Color::red_, "g",
                                                                 &Color::green_, "b", &Color::blue_,
                                                                 "a", &Color::alpha_);
        luaScript.registerType<ivec3, ivec3(int, int, int)>("ivec3", "x", &ivec3::x, "y", &ivec3::y,
                                                            "z", &ivec3::z);
        auto vec3Type = luaScript.registerType<vec3, vec3(float, float, float)>(
            "vec3", "x", &vec3::x, "y", &vec3::y, "z", &vec3::z);
        vec3Type[sol::meta_function::addition] = [](const vec3 &a, const vec3 &b) { return a + b; };
        vec3Type[sol::meta_function::subtraction] = [](const vec3 &a, const vec3 &b) {
            return a - b;
        };

        luaScript.registerEnum<IREasingFunctions>(
            "IREasingFunction", {{"LINEAR_INTERPOLATION", kLinearInterpolation},
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
                                 {"BOUNCE_EASE_IN_OUT", kBounceEaseInOut}});

        registerLuaComponentPack(luaScript);
        luaScript.registerType<IRScript::LuaEntity, IRScript::LuaEntity(EntityId)>(
            "LuaEntity", "entity", [](IRScript::LuaEntity &obj) { return obj.entity; });

        luaScript.lua()["IREntity"] = luaScript.lua().create_table();
        luaScript.registerType<IREntity::CreateEntityCallbackParams,
                               IREntity::CreateEntityCallbackParams(ivec3, vec3)>(
            "CreateEntityCallbackParams", "center", &IREntity::CreateEntityCallbackParams::center,
            "index", &IREntity::CreateEntityCallbackParams::index);

        luaScript.registerCreateEntityBatchFunction<C_Position3D, C_VoxelSetNew, C_PeriodicIdle>(
            "createEntityBatchVoxelPeriodicIdle");
    });

    isRegistered = true;
}
} // namespace IRDefaultCreation
