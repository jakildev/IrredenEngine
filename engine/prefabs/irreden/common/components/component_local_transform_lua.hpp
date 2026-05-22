#ifndef COMPONENT_LOCAL_TRANSFORM_LUA_H
#define COMPONENT_LOCAL_TRANSFORM_LUA_H

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_LocalTransform> = true;

template <> inline void bindLuaType<IRComponents::C_LocalTransform>(LuaScript &luaScript) {
    using IRComponents::C_LocalTransform;
    luaScript.registerType<
        C_LocalTransform,
        C_LocalTransform(IRMath::vec3),
        C_LocalTransform(IRMath::vec3, IRMath::vec4),
        C_LocalTransform(IRMath::vec3, IRMath::vec4, IRMath::vec3)>(
        "C_LocalTransform",
        "translation_x",
        [](C_LocalTransform &obj) { return obj.translation_.x; },
        "translation_y",
        [](C_LocalTransform &obj) { return obj.translation_.y; },
        "translation_z",
        [](C_LocalTransform &obj) { return obj.translation_.z; }
    );
}
} // namespace IRScript

#endif /* COMPONENT_LOCAL_TRANSFORM_LUA_H */
