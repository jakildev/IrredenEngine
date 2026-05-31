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

// This is why the validation is a shared header and not a mirrored copy: both paths must assign identical ordinals.
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

// Sharing the whole path, not just member validation, is what guarantees build-time and runtime register a given enum identically.
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
