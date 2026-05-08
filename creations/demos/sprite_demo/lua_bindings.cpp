#include "lua_bindings.hpp"
#include "lua_component_pack.hpp"

#include <irreden/ir_engine.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/lua_sprite_namespace.hpp>

namespace IRSpriteDemoCreation {

void registerLuaBindings() {
    static bool isRegistered = false;
    if (isRegistered) {
        return;
    }
    isRegistered = true;

    IREngine::registerLuaBindings([](IRScript::LuaScript &luaScript) {
        using namespace IRMath;
        using namespace IRComponents;

        luaScript.registerType<Color, Color(int, int, int, int)>(
            "Color",
            "r", &Color::red_,
            "g", &Color::green_,
            "b", &Color::blue_,
            "a", &Color::alpha_
        );
        luaScript.registerType<vec2, vec2(float, float)>(
            "vec2",
            "x", &vec2::x,
            "y", &vec2::y
        );
        luaScript.registerType<vec4, vec4(float, float, float, float)>(
            "vec4",
            "x", &vec4::x,
            "y", &vec4::y,
            "z", &vec4::z,
            "w", &vec4::w
        );

        IRSpriteDemoCreation::registerLuaComponentPack(luaScript);
        IRScript::bindSpriteNamespace(luaScript);
    });
}

} // namespace IRSpriteDemoCreation
