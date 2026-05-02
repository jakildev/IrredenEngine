#ifndef COMPONENT_SPRITE_SHEET_LUA_H
#define COMPONENT_SPRITE_SHEET_LUA_H

#include <irreden/render/components/component_sprite_sheet.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_SpriteSheet> = true;

template <> inline void bindLuaType<IRComponents::C_SpriteSheet>(LuaScript &luaScript) {
    using IRComponents::C_SpriteSheet;

    // The frame table and named-animation list are loader-populated and
    // consumed by the animation runtime in C++; Lua drives playback via
    // a prefab-scoped namespace API rather than poking the containers
    // directly. Only the simple POD fields are reflected here.
    luaScript.registerType<C_SpriteSheet, C_SpriteSheet()>(
        "C_SpriteSheet",
        "textureHandle",
        &C_SpriteSheet::textureHandle_,
        "atlasSizePx",
        &C_SpriteSheet::atlasSizePx_
    );
}

} // namespace IRScript

#endif /* COMPONENT_SPRITE_SHEET_LUA_H */
