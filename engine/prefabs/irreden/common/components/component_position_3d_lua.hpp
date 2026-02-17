#ifndef COMPONENT_POSITION_3D_LUA_H
#define COMPONENT_POSITION_3D_LUA_H

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_Position3D> = true;

template <> inline void bindLuaType<IRComponents::C_Position3D>(LuaScript &luaScript) {
    using IRComponents::C_Position3D;
    luaScript
        .registerType<C_Position3D, C_Position3D(float, float, float), C_Position3D(IRMath::vec3)>(
            "C_Position3D", "x", [](C_Position3D &obj) { return obj.pos_.x; }, "y",
            [](C_Position3D &obj) { return obj.pos_.y; }, "z",
            [](C_Position3D &obj) { return obj.pos_.z; });
}
} // namespace IRScript

#endif /* COMPONENT_POSITION_3D_LUA_H */
