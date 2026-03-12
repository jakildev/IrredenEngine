#ifndef COMPONENT_SMOOTH_MOVEMENT_LUA_H
#define COMPONENT_SMOOTH_MOVEMENT_LUA_H

#include <irreden/common/components/component_smooth_movement.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_SmoothMovement> = true;

template <> inline void bindLuaType<IRComponents::C_SmoothMovement>(LuaScript &luaScript) {
    luaScript.registerType<IRComponents::C_SmoothMovement, IRComponents::C_SmoothMovement()>(
        "C_SmoothMovement"
    );
}
} // namespace IRScript

#endif /* COMPONENT_SMOOTH_MOVEMENT_LUA_H */
