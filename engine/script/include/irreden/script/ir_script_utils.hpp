#pragma once

#include <irreden/ir_math.hpp>
#include <sol/sol.hpp>

namespace IRScript {

// sol::optional<float> per key — get_or<T> overload resolution is ambiguous with mixed
// string/integer key types.
inline IRMath::vec3 vec3FromLua(sol::object obj) {
    if (obj.is<IRMath::vec3>())
        return obj.as<IRMath::vec3>();
    if (obj.is<sol::table>()) {
        sol::table t = obj.as<sol::table>();
        auto pickFloat = [&t](const char *key, int idx) -> float {
            if (sol::optional<float> v = t[key])
                return *v;
            if (sol::optional<float> v = t[idx])
                return *v;
            return 0.0f;
        };
        return {pickFloat("x", 1), pickFloat("y", 2), pickFloat("z", 3)};
    }
    return {0.0f, 0.0f, 0.0f};
}

// Returns identity-quat (`vec4(0, 0, 0, 1)`) for nil/unrecognized input,
// per the engine convention that quats are stored as `vec4(qx, qy, qz, qw)`
// with `.w` the scalar. Accepts either an `IRMath::vec4` userdata or a
// `{x,y,z,w}` / `{1,2,3,4}` table. Asymmetric default vs. `vec3FromLua`
// (which zero-defaults) because zero-quat is degenerate — every modifier
// caller would have to override the default anyway.
inline IRMath::vec4 quatFromLua(sol::object obj) {
    if (obj.is<IRMath::vec4>())
        return obj.as<IRMath::vec4>();
    if (obj.is<sol::table>()) {
        sol::table t = obj.as<sol::table>();
        auto pickFloat = [&t](const char *key, int idx, float fallback) -> float {
            if (sol::optional<float> v = t[key])
                return *v;
            if (sol::optional<float> v = t[idx])
                return *v;
            return fallback;
        };
        return {
            pickFloat("x", 1, 0.0f),
            pickFloat("y", 2, 0.0f),
            pickFloat("z", 3, 0.0f),
            pickFloat("w", 4, 1.0f)
        };
    }
    return {0.0f, 0.0f, 0.0f, 1.0f};
}

} // namespace IRScript
