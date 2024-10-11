/*
 * Project: Irreden Engine
 * File: lua_script.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef LUA_SCRIPT_H
#define LUA_SCRIPT_H

// #include <lua54/lua.hpp>

#define SOL_ALL_SAFETIES_ON 1
#include <sol/sol.hpp>

std::vector<std::string> splitString(
    const std::string& input,
    char delimiter
);

class LuaScript {
public:
    LuaScript(const char* filename);

    ~LuaScript();
private:
    sol::state lua;
};

// template <>
// int LuaScript::returnLuaValue(int defaultValue) {
//     if (lua_isinteger(L, -1)) {
//         int value = lua_tointeger(L, -1);
//         lua_pop(L, 1);  // Clean up the stack
//         return value;
//     }
//     IR_LOG_WARN("Lua value is not an integer, returing default {}.", defaultValue);
//     return defaultValue;
// }

// template <>
// double LuaScript::returnLuaValue(double defaultValue) {
//     if (lua_isnumber(L, -1)) {
//         double value = lua_tonumber(L, -1);
//         lua_pop(L, 1);  // Clean up the stack
//         return value;
//     }
//     IR_LOG_WARN("Lua value is not a number, returing default {}.", defaultValue);
//     return defaultValue;
// }

std::vector<std::string> splitString(const std::string& input, char delimiter) {
    std::vector<std::string> keys;
    std::stringstream ss(input);
    std::string item;

    while (std::getline(ss, item, delimiter)) {
        keys.push_back(item);  // Add each split item to the vector
    }

    return keys;
}

#endif