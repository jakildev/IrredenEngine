#ifndef COMPONENT_BIND_POINTS_LUA_H
#define COMPONENT_BIND_POINTS_LUA_H

// Exposes hasPoint/pointCount; entity:bindPoint(name) lives on LuaEntity in lua_script.cpp.

#include <irreden/voxel/components/component_bind_points.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_BindPoints> = true;

template <> inline void bindLuaType<IRComponents::C_BindPoints>(LuaScript &luaScript) {
    luaScript.registerType<IRComponents::C_BindPoints, IRComponents::C_BindPoints()>(
        "C_BindPoints",
        "hasPoint",
        [](IRComponents::C_BindPoints &self, const std::string &name) {
            return self.hasPoint(name);
        },
        "pointCount",
        [](IRComponents::C_BindPoints &self) {
            return static_cast<std::int64_t>(self.points_.size());
        }
    );
}
} // namespace IRScript

#endif /* COMPONENT_BIND_POINTS_LUA_H */
