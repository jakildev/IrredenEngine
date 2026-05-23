#ifndef SPRITE_DEMO_LUA_COMPONENT_PACK_H
#define SPRITE_DEMO_LUA_COMPONENT_PACK_H

#include <irreden/common/components/component_local_transform_lua.hpp>
#include <irreden/render/components/component_sprite_lua.hpp>
#include <irreden/render/components/component_sprite_sheet_lua.hpp>
#include <irreden/render/components/component_sprite_animation_lua.hpp>

namespace IRSpriteDemoCreation {
inline void registerLuaComponentPack(IRScript::LuaScript &luaScript) {
    using namespace IRComponents;
    luaScript
        .registerTypesFromTraits<C_LocalTransform, C_Sprite, C_SpriteSheet, C_SpriteAnimation>();
}
} // namespace IRSpriteDemoCreation

#endif /* SPRITE_DEMO_LUA_COMPONENT_PACK_H */
