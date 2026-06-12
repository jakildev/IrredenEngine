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

// `sol::object` → `IRMath::ivec3`. Mirrors `vec3FromLua` but reads integer
// components (truncating toward zero on a fractional Lua number, matching a
// C++ `static_cast<int>`). Accepts an `IRMath::ivec3` userdata or an
// `{x,y,z}` / `{1,2,3}` table; zero-defaults for nil/unrecognized input.
inline IRMath::ivec3 ivec3FromLua(sol::object obj) {
    if (obj.is<IRMath::ivec3>())
        return obj.as<IRMath::ivec3>();
    if (obj.is<sol::table>()) {
        sol::table t = obj.as<sol::table>();
        auto pickInt = [&t](const char *key, int idx) -> int {
            if (sol::optional<int> v = t[key])
                return *v;
            if (sol::optional<int> v = t[idx])
                return *v;
            return 0;
        };
        return {pickInt("x", 1), pickInt("y", 2), pickInt("z", 3)};
    }
    return {0, 0, 0};
}

// `sol::object` → `IRMath::Color`. Accepts an `IRMath::Color` userdata or a
// `{r,g,b[,a]}` / `{1,2,3[,4]}` table with 0-255 components; the keyed and
// indexed spellings both work. Missing channels default to 255, so an omitted
// alpha is opaque and a fully-omitted color is opaque white (matching the
// indexed-table color convention the IRText creation binding already uses).
inline IRMath::Color colorFromLua(sol::object obj) {
    if (obj.is<IRMath::Color>())
        return obj.as<IRMath::Color>();
    if (obj.is<sol::table>()) {
        sol::table t = obj.as<sol::table>();
        auto pickByte = [&t](const char *key, int idx) -> int {
            if (sol::optional<int> v = t[key])
                return *v;
            if (sol::optional<int> v = t[idx])
                return *v;
            return 255;
        };
        return IRMath::Color(
            pickByte("r", 1),
            pickByte("g", 2),
            pickByte("b", 3),
            pickByte("a", 4)
        );
    }
    return IRMath::Color(255, 255, 255, 255);
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
