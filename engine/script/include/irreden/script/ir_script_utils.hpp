#pragma once

#include <irreden/ir_math.hpp>
#include <sol/sol.hpp>

namespace IRScript {

/// Extract an IRMath::vec3 from a Lua value. Accepts an IRMath::vec3 userdata
/// or a {x,y,z} / {1,2,3} table. Returns {0,0,0} for nil/none/absent.
/// Validate the type at the callsite before calling this helper — it
/// zero-defaults on unrecognized types so bad-type errors need a caller-side
/// check to surface a descriptive message.
///
/// Named-or-indexed field lookup uses sol::optional<float> per key because
/// sol2's get_or<T> overload resolution is ambiguous with mixed string/integer
/// key types.
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

} // namespace IRScript
