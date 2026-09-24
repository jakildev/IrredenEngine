#ifndef COMPONENT_FOG_FIELD_LUA_H
#define COMPONENT_FOG_FIELD_LUA_H

#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_FogField> = true;

template <> inline void bindLuaType<IRComponents::C_FogField>(LuaScript &luaScript) {
    using IRComponents::C_FogField;
    luaScript.registerType<C_FogField, C_FogField()>("C_FogField");
}

} // namespace IRScript

#endif /* COMPONENT_FOG_FIELD_LUA_H */
