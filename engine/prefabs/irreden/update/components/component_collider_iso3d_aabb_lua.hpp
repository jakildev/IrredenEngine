#ifndef COMPONENT_COLLIDER_ISO3D_AABB_LUA_H
#define COMPONENT_COLLIDER_ISO3D_AABB_LUA_H

#include <irreden/update/components/component_collider_iso3d_aabb.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_ColliderIso3DAABB> = true;

template <>
inline void bindLuaType<IRComponents::C_ColliderIso3DAABB>(LuaScript &luaScript) {
    luaScript.registerType<IRComponents::C_ColliderIso3DAABB,
                           IRComponents::C_ColliderIso3DAABB(IRMath::vec3, IRMath::vec3),
                           IRComponents::C_ColliderIso3DAABB(float, float, float),
                           IRComponents::C_ColliderIso3DAABB()>(
        "C_ColliderIso3DAABB", "halfExtents",
        &IRComponents::C_ColliderIso3DAABB::halfExtents_, "centerOffset",
        &IRComponents::C_ColliderIso3DAABB::centerOffset_);
}
} // namespace IRScript

#endif /* COMPONENT_COLLIDER_ISO3D_AABB_LUA_H */
