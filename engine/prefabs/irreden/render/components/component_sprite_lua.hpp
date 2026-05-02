#ifndef COMPONENT_SPRITE_LUA_H
#define COMPONENT_SPRITE_LUA_H

#include <irreden/render/components/component_sprite.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_Sprite> = true;

template <> inline void bindLuaType<IRComponents::C_Sprite>(LuaScript &luaScript) {
    using IRComponents::C_Sprite;

    luaScript.registerType<
        C_Sprite,
        C_Sprite(IRRender::ResourceId, IRMath::vec2, IRMath::vec4, IRMath::vec2, IRMath::Color),
        C_Sprite(IRRender::ResourceId, IRMath::vec2),
        C_Sprite()>(
        "C_Sprite",
        "textureHandle",
        &C_Sprite::textureHandle_,
        "size",
        &C_Sprite::size_,
        "uvRect",
        &C_Sprite::uvRect_,
        "anchor",
        &C_Sprite::anchor_,
        "tint",
        &C_Sprite::tint_
    );
}

} // namespace IRScript

#endif /* COMPONENT_SPRITE_LUA_H */
