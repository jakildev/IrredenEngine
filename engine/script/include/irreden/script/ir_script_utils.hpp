#pragma once

#include <irreden/ir_math.hpp>
#include <sol/sol.hpp>

#include <cctype>
#include <functional>
#include <string>
#include <string_view>
#include <utility>

namespace IRScript {

// Every `*FromLua` helper below matches its concrete usertype FIRST, then the
// EXACT Lua table type, then falls through to its documented default. The
// order is the contract, not a style choice: `sol::object::is<sol::table>()` is TRUE for
// userdata as well — sol2 treats userdata as table-like — so a table-first
// check admits every userdata regardless of the usertype test above it. A
// wrong-typed vector (a `vec2` where a `vec3` is wanted) then reaches the table
// branch, where it either raises a bare "attempt to index a userdata value"
// from inside the helper (metatable-less userdata) or silently coerces through
// the registered usertype's `__index` — both in place of the zero-default the
// contract promises. `get_type() == sol::type::table` is the check that
// discriminates (rule in engine/script/CLAUDE.md).
//
// Component TABLES stay arity-blind by design: `{x = 1, y = 2}` passed where a
// vec3 is wanted zero-fills `z`. Only the userdata path carries enough type
// information to discriminate, so callers that need a bad-type ERROR rather
// than a default still validate at the callsite before calling the helper.

// sol::optional<float> per key — get_or<T> overload resolution is ambiguous with mixed
// string/integer key types.
inline IRMath::vec3 vec3FromLua(sol::object obj) {
    if (obj.is<IRMath::vec3>())
        return obj.as<IRMath::vec3>();
    if (obj.get_type() == sol::type::table) {
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

// `sol::object` → `IRMath::vec2`. Mirrors `vec3FromLua` one component down:
// accepts an `IRMath::vec2` userdata or an `{x,y}` / `{1,2}` table, and
// zero-defaults for nil/unrecognized input.
inline IRMath::vec2 vec2FromLua(sol::object obj) {
    if (obj.is<IRMath::vec2>())
        return obj.as<IRMath::vec2>();
    if (obj.get_type() == sol::type::table) {
        sol::table t = obj.as<sol::table>();
        auto pickFloat = [&t](const char *key, int idx) -> float {
            if (sol::optional<float> v = t[key])
                return *v;
            if (sol::optional<float> v = t[idx])
                return *v;
            return 0.0f;
        };
        return {pickFloat("x", 1), pickFloat("y", 2)};
    }
    return {0.0f, 0.0f};
}

// `sol::object` → `IRMath::vec4`. Mirrors `vec3FromLua` one component up:
// accepts an `IRMath::vec4` userdata or a table keyed `{x,y,z,w}` **or**
// `{r,g,b,a}` (the same vec4 carries positions and float colors), or indexed
// `{1,2,3,4}`. Zero-defaults for nil/unrecognized input and for any missing
// component, per the `vec3FromLua` contract.
//
// Distinct from `quatFromLua`, which reads the same `IRMath::vec4` storage but
// identity-defaults (`w = 1`) because a zero quat is degenerate. Pick by
// meaning: `vec4FromLua` for a position/color 4-vector, `quatFromLua` for a
// rotation.
inline IRMath::vec4 vec4FromLua(sol::object obj) {
    if (obj.is<IRMath::vec4>())
        return obj.as<IRMath::vec4>();
    if (obj.get_type() == sol::type::table) {
        sol::table t = obj.as<sol::table>();
        auto pickFloat = [&t](const char *key, const char *altKey, int idx) -> float {
            if (sol::optional<float> v = t[key])
                return *v;
            if (sol::optional<float> v = t[altKey])
                return *v;
            if (sol::optional<float> v = t[idx])
                return *v;
            return 0.0f;
        };
        return {
            pickFloat("x", "r", 1),
            pickFloat("y", "g", 2),
            pickFloat("z", "b", 3),
            pickFloat("w", "a", 4)
        };
    }
    return {0.0f, 0.0f, 0.0f, 0.0f};
}

// sol2 treats userdata as table-like, so the concrete usertype check must
// precede the exact table check or a wrong vector type is silently accepted.
template <typename VecT>
inline void requireVecShape(const sol::object &obj, const char *context, const char *typeName) {
    if (obj.is<VecT>() || obj.get_type() == sol::type::table) {
        return;
    }
    throw sol::error{
        std::string{context} + " must be an IRMath " + typeName + " userdata or a component table"
    };
}

inline IRMath::vec2 requireVec2(const sol::object &obj, const char *context) {
    requireVecShape<IRMath::vec2>(obj, context, "vec2");
    return vec2FromLua(obj);
}

inline IRMath::vec3 requireVec3(const sol::object &obj, const char *context) {
    requireVecShape<IRMath::vec3>(obj, context, "vec3");
    return vec3FromLua(obj);
}

inline IRMath::vec4 requireVec4(const sol::object &obj, const char *context) {
    requireVecShape<IRMath::vec4>(obj, context, "vec4");
    return vec4FromLua(obj);
}

// `sol::object` → `IRMath::ivec3`. Mirrors `vec3FromLua` but reads integer
// components (truncating toward zero on a fractional Lua number, matching a
// C++ `static_cast<int>`). Accepts an `IRMath::ivec3` userdata or an
// `{x,y,z}` / `{1,2,3}` table; zero-defaults for nil/unrecognized input.
inline IRMath::ivec3 ivec3FromLua(sol::object obj) {
    if (obj.is<IRMath::ivec3>())
        return obj.as<IRMath::ivec3>();
    if (obj.get_type() == sol::type::table) {
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
    if (obj.get_type() == sol::type::table) {
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
    if (obj.get_type() == sol::type::table) {
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

// `value` as a double-quoted Lua string literal.
inline std::string luaStringLiteral(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size() + 2);
    escaped.push_back('"');
    for (char c : value) {
        switch (c) {
        case '\\':
            escaped += "\\\\";
            break;
        case '"':
            escaped += "\\\"";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(c);
            break;
        }
    }
    escaped.push_back('"');
    return escaped;
}

// Whether `name` can be written bare as a table-constructor key or after a
// dot: a Lua 5.1 name that is not a reserved word.
inline bool isLuaIdentifier(std::string_view name) {
    static constexpr std::string_view kReserved[] = {
        "and", "break",    "do",     "else", "elseif", "end",   "false",
        "for", "function", "if",     "in",   "local",  "nil",   "not",
        "or",  "repeat",   "return", "then", "true",   "until", "while",
    };
    if (name.empty() ||
        (std::isalpha(static_cast<unsigned char>(name[0])) == 0 && name[0] != '_')) {
        return false;
    }
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
            return false;
        }
    }
    for (std::string_view reserved : kReserved) {
        if (name == reserved) {
            return false;
        }
    }
    return true;
}

// `key` as the left side of a table-constructor field: bare when it is an
// identifier, `["..."]` otherwise. Every writer of Lua table source spells a
// string key through this, so any string a table can hold round-trips.
inline std::string luaTableKey(std::string_view key) {
    if (isLuaIdentifier(key)) {
        return std::string(key);
    }
    return "[" + luaStringLiteral(key) + "]";
}

namespace detail {

/// Wraps a capturing callable as a `std::function` before it is bound to a Lua
/// key. Use it for every bound callable whose captures have a non-trivial
/// destructor (`std::function`, `std::shared_ptr`, containers).
///
/// sol2 stores a stateful functor as userdata and finds its `__gc` finalizer
/// in a metatable keyed by the demangled type name, reusing the first
/// metatable registered under a name. GCC names a lambda
/// `Enclosing(args)::<lambda(params)>` with no discriminator, so two capturing
/// lambdas with one parameter list in one function share a name, and the
/// second one's captures are destroyed as the first one's type at `lua_close`.
/// After wrapping, equal names mean equal `std::function` types, so the shared
/// finalizer is always the right one. Captureless lambdas bind as function
/// pointers, and trivially destructible captures need no finalizer, so neither
/// needs this. CTAD needs a single non-template `operator()`.
template <typename F> auto statefulLuaFunction(F &&fn) {
    return std::function{std::forward<F>(fn)};
}

} // namespace detail

} // namespace IRScript
