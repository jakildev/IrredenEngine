#include "lua_bindings.hpp"
#include "lua_component_pack.hpp"

#include <irreden/ir_engine.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/lua_sprite_namespace.hpp>
#include <irreden/script/lua_bindings_default.hpp>

namespace IRSpriteDemoCreation {

void registerLuaBindings() {
    static bool isRegistered = false;
    if (isRegistered) {
        return;
    }
    isRegistered = true;

    IREngine::registerLuaBindings([](IRScript::LuaScript &luaScript) {
        IRScript::registerStandardBindings(luaScript);

        IRSpriteDemoCreation::registerLuaComponentPack(luaScript);
        IRScript::bindSpriteNamespace(luaScript);
    });
}

} // namespace IRSpriteDemoCreation
