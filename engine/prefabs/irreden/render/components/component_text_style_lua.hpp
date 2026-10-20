#ifndef COMPONENT_TEXT_STYLE_LUA_H
#define COMPONENT_TEXT_STYLE_LUA_H

#include <irreden/render/components/component_text_style.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/lua_text_bindings.hpp>

#include <string>

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
    // Voxel letters: voxelSize > 0 turns them on (0 = flat glyphs).
    type["voxelSize"] = sol::property(
        [](const C_TextStyle &style) { return style.voxel_.voxelSize_; },
        [](C_TextStyle &style, int size) {
            if (size < 0 || size > IRRender::kMaxVoxelTextSize) {
                throw sol::error{
                    "C_TextStyle.voxelSize must be 0.." +
                    std::to_string(IRRender::kMaxVoxelTextSize)
                };
            }
            style.voxel_.voxelSize_ = size;
        }
    );
    type["voxelDepth"] = sol::property(
        [](const C_TextStyle &style) { return style.voxel_.depth_; },
        [](C_TextStyle &style, int depth) {
            if (depth < 1 || depth > IRRender::kMaxVoxelTextDepth) {
                throw sol::error{
                    "C_TextStyle.voxelDepth must be 1.." +
                    std::to_string(IRRender::kMaxVoxelTextDepth)
                };
            }
            style.voxel_.depth_ = depth;
        }
    );
    // An IRText.VoxelFacing value; the setter raises on anything else.
    type["voxelFacing"] = sol::property(
        [](const C_TextStyle &style) { return static_cast<lua_Integer>(style.voxel_.facing_); },
        [](C_TextStyle &style, sol::object value) {
            style.voxel_.facing_ = voxelFacingFromLua(value, "C_TextStyle.voxelFacing");
        }
    );
    type["faceX"] = sol::property(
        [](const C_TextStyle &style) { return style.faceColors_.x_; },
        [](C_TextStyle &style, Color color) { style.faceColors_.x_ = color; }
    );
    type["faceY"] = sol::property(
        [](const C_TextStyle &style) { return style.faceColors_.y_; },
        [](C_TextStyle &style, Color color) { style.faceColors_.y_ = color; }
    );
    type["faceZ"] = sol::property(
        [](const C_TextStyle &style) { return style.faceColors_.z_; },
        [](C_TextStyle &style, Color color) { style.faceColors_.z_ = color; }
    );
    // Sets all three face colours from one top colour (shadedVoxelFaceColors).
    type["shadeFaces"] = [](C_TextStyle &style, Color top) {
        style.faceColors_ = IRRender::shadedVoxelFaceColors(top);
    };
}
} // namespace IRScript

#endif /* COMPONENT_TEXT_STYLE_LUA_H */
