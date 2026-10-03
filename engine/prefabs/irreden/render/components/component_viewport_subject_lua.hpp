#ifndef COMPONENT_VIEWPORT_SUBJECT_LUA_H
#define COMPONENT_VIEWPORT_SUBJECT_LUA_H

#include <irreden/render/components/component_viewport_subject.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_ViewportSubject> = true;

template <> inline void bindLuaType<IRComponents::C_ViewportSubject>(LuaScript &luaScript) {
    using IRComponents::C_ViewportSubject;
    luaScript.registerType<
        C_ViewportSubject,
        C_ViewportSubject(IREntity::EntityId),
        C_ViewportSubject()>("C_ViewportSubject", "viewport", &C_ViewportSubject::viewport_);
}

} // namespace IRScript

#endif /* COMPONENT_VIEWPORT_SUBJECT_LUA_H */
