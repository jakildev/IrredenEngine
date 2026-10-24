#ifndef COMPONENT_CONTACT_EVENT_LUA_H
#define COMPONENT_CONTACT_EVENT_LUA_H

#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_ContactEvent> = true;

template <> inline void bindLuaType<IRComponents::C_ContactEvent>(LuaScript &luaScript) {
    // `C_ContactEvent.new()` and `C_ContactEvent.new(bornTouching)`.
    luaScript.registerType<
        IRComponents::C_ContactEvent,
        IRComponents::C_ContactEvent(),
        IRComponents::C_ContactEvent(bool)>("C_ContactEvent");
}
} // namespace IRScript

#endif /* COMPONENT_CONTACT_EVENT_LUA_H */
