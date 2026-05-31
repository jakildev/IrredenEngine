#ifndef LUA_ENUM_DEF_H
#define LUA_ENUM_DEF_H

#ifndef SOL_ALL_SAFETIES_ON
#define SOL_ALL_SAFETIES_ON 1
#endif
#include <sol/sol.hpp>

#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_set>

namespace IRScript::detail {

// Validate a Lua-defined enum member list and build its name->ordinal
// table. Shared verbatim by the runtime `IREnum.register` binding (EVAL,
// engine/script/src/lua_script.cpp) and the build-time codegen stub
// (CODEGEN, cmake/lua_codegen/main.cpp) so both validate identically AND
// assign the same ordinals — a divergence would let a codegen'd component
// default (captured from `MyEnum.MEMBER` at build time) disagree with the
// value the same .lua produces at runtime, a silent correctness bug. This
// is why the validation is a shared header and not a mirrored copy.
//
// `members` must be a Lua array (1..#members) of unique, non-empty
// strings. Members map to 0-based ordinals in declaration order, mirroring
// the default numbering of a C++ `enum class` and the 0-based field
// `index` of `IRComponent.register`. Returns a fresh Lua table mapping each
// member name to its integer ordinal — the same shape the C++
// `registerEnum`/`new_enum` surface produces, so a Lua-defined enum is
// usable anywhere a C++-bound enum is (`MyEnum.MEMBER`).
//
// Throws std::runtime_error with a diagnostic on: empty enum name, empty
// or non-array member list, a non-string / empty-string member, or a
// duplicate member name. The engine-side binding runs with
// SOL_EXCEPTIONS_ALWAYS_UNSAFE, so sol2 forwards what() to the Lua caller;
// the codegen driver prints what() before the exception unwinds (LuaJIT
// drops it there) and aborts the build with the same message.
inline sol::table buildLuaEnumTable(
    sol::state_view lua, const std::string &enumName, const sol::table &members
) {
    if (enumName.empty()) {
        throw std::runtime_error{"IREnum.register: enum name must be a non-empty string"};
    }

    const std::size_t count = members.size();
    if (count == 0) {
        throw std::runtime_error{
            "IREnum.register: '" + enumName +
            "' has no members (expected an array of strings, e.g. { \"EFFECT\", \"SYNTH\" })"
        };
    }

    sol::table result = lua.create_table();
    std::unordered_set<std::string> seen;
    seen.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        sol::object entry = members[i];
        if (entry.get_type() != sol::type::string) {
            throw std::runtime_error{
                "IREnum.register: '" + enumName + "' member #" + std::to_string(i) +
                " is not a string"
            };
        }
        std::string member = entry.as<std::string>();
        if (member.empty()) {
            throw std::runtime_error{
                "IREnum.register: '" + enumName + "' member #" + std::to_string(i) +
                " is an empty string"
            };
        }
        if (!seen.insert(member).second) {
            throw std::runtime_error{
                "IREnum.register: '" + enumName + "' has duplicate member '" + member + "'"
            };
        }
        result[member] = static_cast<lua_Integer>(i - 1);
    }
    return result;
}

// The full IREnum.register implementation, shared verbatim by the runtime
// binding (engine/script/src/lua_script.cpp) and the codegen stub
// (cmake/lua_codegen/main.cpp) — sharing the whole path, not just member
// validation, is what guarantees build-time and runtime register a given
// enum identically. Rejects the reserved name "register" (it is the
// function key on the IREnum table), rejects a duplicate enum name (tracked
// in `registered`), builds + validates the table via buildLuaEnumTable,
// records the name, stores the table at `IREnum.<enumName>` for cross-file
// reference, and returns it. `registered` is the caller's set of
// already-registered enum names (a LuaScript member at runtime, a local in
// the codegen tool). Throws std::runtime_error on any validation failure;
// the name is recorded and the table stored only after a successful build,
// so a rejected call leaves the name free to retry.
inline sol::table registerLuaEnum(
    sol::state_view lua,
    std::unordered_set<std::string> &registered,
    const std::string &enumName,
    const sol::table &members
) {
    if (enumName == "register") {
        throw std::runtime_error{"IREnum.register: 'register' is a reserved name"};
    }
    if (registered.count(enumName) != 0) {
        throw std::runtime_error{"IREnum.register: '" + enumName + "' is already registered"};
    }
    sol::table enumTable = buildLuaEnumTable(lua, enumName, members);
    registered.insert(enumName);
    lua["IREnum"][enumName] = enumTable;
    return enumTable;
}

} // namespace IRScript::detail

#endif /* LUA_ENUM_DEF_H */
