#ifndef LUA_TEXT_BINDINGS_H
#define LUA_TEXT_BINDINGS_H

// IRText Lua bindings — the glyph-set enum and text measurement.
//
//   IRText.Size.LARGE, IRText.Size.SMALL
//   local w, h = IRText.measure(text, size?, wrapWidth?)  -- GUI trixels, fontSize 1
//
// Every Lua call that takes a text size reads it through textSizeFromLua, so
// they share one validation and one error message.

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

#include <irreden/ir_math.hpp>
#include <irreden/script/lua_script.hpp>

#include <irreden/render/trixel_font.hpp>
#include <irreden/render/trixel_text.hpp>

#include <string>
#include <tuple>

namespace IRScript {

// Absent or nil reads LARGE; anything else but an `IRText.Size` value raises
// `<caller>: size must be ...`.
inline IRRender::TextSize textSizeFromLua(const sol::object &value, const char *caller) {
    if (!value.valid()) {
        return IRRender::TextSize::LARGE;
    }
    if (value.get_type() == sol::type::number) {
        const double number = value.as<double>();
        const auto integer = static_cast<lua_Integer>(number);
        if (static_cast<double>(integer) == number) {
            switch (integer) {
            case static_cast<lua_Integer>(IRRender::TextSize::LARGE):
                return IRRender::TextSize::LARGE;
            case static_cast<lua_Integer>(IRRender::TextSize::SMALL):
                return IRRender::TextSize::SMALL;
            default:
                break;
            }
        }
    }
    throw sol::error{std::string(caller) + ": size must be IRText.Size.LARGE or IRText.Size.SMALL"};
}

namespace detail {

inline void bindText(LuaScript &script) {
    sol::state &lua = script.lua();
    // Extend (never replace) IRText so a creation's own entries survive.
    const sol::object existing = lua["IRText"];
    if (!existing.valid()) {
        lua["IRText"] = lua.create_table();
    } else if (existing.get_type() != sol::type::table) {
        throw sol::error{"bindLuaText: global IRText exists and is not a table"};
    }
    sol::table text = lua["IRText"];

    sol::table sizes = lua.create_table();
#define IR_BIND_TEXT_SIZE(name) sizes[#name] = static_cast<lua_Integer>(IRRender::TextSize::name)
    IR_BIND_TEXT_SIZE(LARGE);
    IR_BIND_TEXT_SIZE(SMALL);
#undef IR_BIND_TEXT_SIZE
    text["Size"] = sizes;

    text["measure"] = [](const std::string &str, sol::object size, sol::optional<int> wrapWidth) {
        const IRMath::ivec2 extent = IRRender::measureText(
            str,
            wrapWidth.value_or(0),
            textSizeFromLua(size, "IRText.measure")
        );
        return std::make_tuple(extent.x, extent.y);
    };
}

} // namespace detail

} // namespace IRScript

#endif /* LUA_TEXT_BINDINGS_H */
