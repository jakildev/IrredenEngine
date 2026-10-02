#ifndef COMPONENT_FOG_EXEMPT_LUA_H
#define COMPONENT_FOG_EXEMPT_LUA_H

#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {

template <> inline constexpr bool kHasLuaBinding<IRComponents::C_FogExempt> = true;

template <> inline void bindLuaType<IRComponents::C_FogExempt>(LuaScript &luaScript) {
    using IRComponents::C_FogExempt;
    luaScript.registerType<C_FogExempt, C_FogExempt()>("C_FogExempt");
}

} // namespace IRScript

#endif /* COMPONENT_FOG_EXEMPT_LUA_H */
