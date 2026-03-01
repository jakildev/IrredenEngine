#ifndef COMPONENT_TEXT_SEGMENT_LUA_H
#define COMPONENT_TEXT_SEGMENT_LUA_H

#include <irreden/render/components/component_text_segment.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_TextSegment> = true;

template <> inline void bindLuaType<IRComponents::C_TextSegment>(LuaScript &luaScript) {
    using IRComponents::C_TextSegment;
    luaScript.registerType<C_TextSegment, C_TextSegment(std::string)>(
        "C_TextSegment",
        "text",
        &C_TextSegment::text_
    );
}
} // namespace IRScript

#endif /* COMPONENT_TEXT_SEGMENT_LUA_H */
