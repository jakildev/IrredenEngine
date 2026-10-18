#ifndef COMPONENT_TEXT_STYLE_LUA_H
#define COMPONENT_TEXT_STYLE_LUA_H

#include <irreden/render/components/component_text_style.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/lua_text_bindings.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_TextStyle> = true;

template <> inline void bindLuaType<IRComponents::C_TextStyle>(LuaScript &luaScript) {
    using IRComponents::C_TextStyle;
    using IRComponents::TextAlignH;
    using IRComponents::TextAlignV;
    using IRMath::Color;
    sol::usertype<C_TextStyle> type = luaScript.registerType<
        C_TextStyle,
        C_TextStyle(Color, int, TextAlignH, TextAlignV, int, int, int),
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
        &C_TextStyle::boxHeight_,
        "fontSize",
        &C_TextStyle::fontSize_
    );
    // An IRText.Size value; the setter raises on anything else.
    type["textSize"] = sol::property(
        [](const C_TextStyle &style) { return static_cast<lua_Integer>(style.textSize_); },
        [](C_TextStyle &style, sol::object value) {
            style.textSize_ = textSizeFromLua(value, "C_TextStyle.textSize");
        }
    );
}
} // namespace IRScript

#endif /* COMPONENT_TEXT_STYLE_LUA_H */
