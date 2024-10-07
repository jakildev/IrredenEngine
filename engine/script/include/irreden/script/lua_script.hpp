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

#include <lua54/lua.hpp> // hpp file contains the extern "C" directive


std::vector<std::string> splitString(
    const std::string& input,
    char delimiter
);

class LuaScript {
public:
    LuaScript(const char* filename, bool withLibs = true);
    inline lua_State* getState() { return L; }

    bool getTableField(
        const char* tableName,
        const char* key
    );
    bool getNestedTable(
        const char* tableName,
        const std::initializer_list<const char*>& keys
    );

    template <typename T>
    T getValue(const char* valuePath, T defaultValue = T()) {
        // split the valuePath into keys via '.'
        std::vector<std::string> keys = splitString(valuePath, '.');
        lua_getglobal(L, *keys.begin());
        for (auto it = keys.begin() + 1; it != keys.end(); ++it) {
            if (!lua_istable(L, -1)) {
                lua_pop(L, 1);  // Cleanup if table not found
                return defaultValue;
            }
            lua_getfield(L, -1, key);  // Get the next field in the path
            lua_remove(L, -2);         // Remove the parent table from the stack
        }
        return returnLuaValue<T>(defaultValue);
    }

    template <typename T>
    T returnLuaValue(T defaultValue);

    ~LuaScript();
private:
    lua_State *L;
};

template <>
int LuaScript::returnLuaValue(int defaultValue) {
    if (lua_isinteger(L, -1)) {
        int value = lua_tointeger(L, -1);
        lua_pop(L, 1);  // Clean up the stack
        return value;
    }
    IR_LOG_WARN("Lua value is not an integer, returing default {}.", defaultValue);
    return defaultValue;
}

template <>
double LuaScript::returnLuaValue(double defaultValue) {
    if (lua_isnumber(L, -1)) {
        double value = lua_tonumber(L, -1);
        lua_pop(L, 1);  // Clean up the stack
        return value;
    }
    IR_LOG_WARN("Lua value is not a number, returing default {}.", defaultValue);
    return defaultValue;
}

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