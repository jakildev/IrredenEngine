#ifndef COMPONENT_HITBOX_2D_LUA_H
#define COMPONENT_HITBOX_2D_LUA_H

#include <irreden/input/components/component_hitbox_2d.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_HitBox2D> = true;

template <> inline void bindLuaType<IRComponents::C_HitBox2D>(LuaScript &luaScript) {
    using IRComponents::C_HitBox2D;
    luaScript.registerType<C_HitBox2D, C_HitBox2D(), C_HitBox2D(float, float)>(
        "C_HitBox2D",
        "halfWidth",
        [](C_HitBox2D &obj) { return obj.halfExtent_.x; },
        "halfHeight",
        [](C_HitBox2D &obj) { return obj.halfExtent_.y; },
        "padding",
        &C_HitBox2D::padding_,
        "hovered",
        &C_HitBox2D::hovered_,
        "enabled",
        &C_HitBox2D::enabled_
    );
}
} // namespace IRScript

#endif /* COMPONENT_HITBOX_2D_LUA_H */
