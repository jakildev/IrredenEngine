#ifndef COMPONENT_COLLISION_LAYER_LUA_H
#define COMPONENT_COLLISION_LAYER_LUA_H

#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_CollisionLayer> = true;

template <> inline void bindLuaType<IRComponents::C_CollisionLayer>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_CollisionLayer,
        IRComponents::C_CollisionLayer(std::uint32_t, std::uint32_t, bool),
        IRComponents::C_CollisionLayer()>(
        "C_CollisionLayer",
        "layer",
        &IRComponents::C_CollisionLayer::layer_,
        "collidesWithMask",
        &IRComponents::C_CollisionLayer::collidesWithMask_,
        "isSolid",
        &IRComponents::C_CollisionLayer::isSolid_
    );
}
} // namespace IRScript

#endif /* COMPONENT_COLLISION_LAYER_LUA_H */
