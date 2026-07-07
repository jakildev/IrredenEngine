#ifndef COMPONENT_LOCAL_TRANSFORM_LUA_H
#define COMPONENT_LOCAL_TRANSFORM_LUA_H

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_LocalTransform> = true;

template <> inline void bindLuaType<IRComponents::C_LocalTransform>(LuaScript &luaScript) {
    using IRComponents::C_LocalTransform;
    // The scalar convenience getters (translation_x/y/z) stay as the zero-alloc
    // read path and register in the usual key/value list, unchanged.
    sol::usertype<C_LocalTransform> type = luaScript.registerType<
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

    // The math-typed SQT fields bind as read-write `sol::property` pairs: the
    // getter returns a fresh { x, y, z[, w] } table (matching the Lua-defined
    // packed-vec read shape in lua_component_data.hpp), and the setter routes a
    // Lua { x, y, z, w } table (or an IRMath userdata) through the *FromLua
    // converters. A bare `&C_LocalTransform::rotation_` member pointer can't be
    // used — IRMath::vec3/vec4 are not registered Lua usertypes, so Lua could
    // neither construct a value to assign nor read the returned userdata. Writes
    // land in place because `:at(i)` hands Lua a std::ref to the column row (see
    // recordComponentLuaName in lua_script.hpp), so the property setter's `self`
    // is the live row. Convention: engine/script/CLAUDE.md "C++-component
    // per-field writes from Lua".
    //
    // These are added *after* registerType, directly on the returned usertype,
    // so the property_wrapper is pushed as an rvalue. sol2's const-lvalue
    // property pusher (function_types.hpp `unqualified_pusher<property_wrapper>`,
    // the `const property_wrapper<F, G>&` overload) is broken in the vendored
    // version — it references `pw.read`/`pw.write` without calling them and
    // fails to compile; only the rvalue overload is correct. Routing the
    // property in via registerType's by-value key/value pack would hit the
    // broken const path. `sol::this_state` gives each getter the calling Lua
    // state without capturing it.
    type["rotation"] = sol::property(
        [](C_LocalTransform &obj, sol::this_state ts) {
            return sol::state_view{ts}.create_table_with(
                "x",
                obj.rotation_.x,
                "y",
                obj.rotation_.y,
                "z",
                obj.rotation_.z,
                "w",
                obj.rotation_.w
            );
        },
        [](C_LocalTransform &obj, sol::object value) {
            obj.rotation_ = IRScript::quatFromLua(value);
        }
    );
    type["translation"] = sol::property(
        [](C_LocalTransform &obj, sol::this_state ts) {
            return sol::state_view{ts}.create_table_with(
                "x",
                obj.translation_.x,
                "y",
                obj.translation_.y,
                "z",
                obj.translation_.z
            );
        },
        [](C_LocalTransform &obj, sol::object value) {
            obj.translation_ = IRScript::vec3FromLua(value);
        }
    );
    type["scale"] = sol::property(
        [](C_LocalTransform &obj, sol::this_state ts) {
            return sol::state_view{ts}
                .create_table_with("x", obj.scale_.x, "y", obj.scale_.y, "z", obj.scale_.z);
        },
        [](C_LocalTransform &obj, sol::object value) { obj.scale_ = IRScript::vec3FromLua(value); }
    );
}
} // namespace IRScript

#endif /* COMPONENT_LOCAL_TRANSFORM_LUA_H */
