#ifndef COMPONENT_TEXT_STYLE_LUA_H
#define COMPONENT_TEXT_STYLE_LUA_H

#include <irreden/render/components/component_text_style.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_TextStyle> = true;

template <> inline void bindLuaType<IRComponents::C_TextStyle>(LuaScript &luaScript) {
    using IRComponents::C_TextStyle;
    using IRComponents::TextAlignH;
    using IRComponents::TextAlignV;
    using IRMath::Color;
    luaScript.registerType<
        C_TextStyle,
        C_TextStyle(Color, int, TextAlignH, TextAlignV, int, int),
        C_TextStyle(Color, int),
        C_TextStyle()>(
        "C_TextStyle",
        "color",
        &C_TextStyle::color_,
        "wrapWidth",
        &C_TextStyle::wrapWidth_,
        "alignH",
        &C_TextStyle::alignH_,
        "alignV",
        &C_TextStyle::alignV_,
        "boxWidth",
        &C_TextStyle::boxWidth_,
        "boxHeight",
        &C_TextStyle::boxHeight_
    );
}
} // namespace IRScript

#endif /* COMPONENT_TEXT_STYLE_LUA_H */
