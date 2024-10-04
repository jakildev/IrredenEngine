/*
 * Project: Irreden Engine
 * File: lua_script.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_profile.hpp>

#include <irreden/script/lua_script.hpp>

// lua_dofile runs a lua script. Global functions and variables
// can be accessed via the lua stack.
LuaScript::LuaScript(const char* filename, bool withLibs) {
    IRE_LOG_INFO("Creating new lua script from file: {}", filename);
    L = luaL_newstate();
    IR_ASSERT(filename != NULL, "attemped to create LuaScript object with null file" );
    if (withLibs) {
        luaL_openlibs(L);
    }
    int status = luaL_dofile(L, filename);
    IR_ASSERT(status == LUA_OK, "luaL_dofile failed with error");
}

LuaScript::~LuaScript() {
    if(L) lua_close(L);
}

bool LuaScript::getTableField(
    const char* tableName,
    const char* key
) {
    lua_getglobal(L, tableName);
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        IR_LOG_WARN("getTableField: {} is not a table", tableName);
        return false;
    }
    lua_getfield(L, -1, key);
    lua_remove(L, -2);
    return true;
}

bool LuaScript::getNestedTable(
    const char* tableName,
    const std::initializer_list<const char*>& keys
) {
    lua_getglobal(L, tableName);
    for (const char* key : keys) {
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);  // Cleanup if table not found
            return false;
        }
        lua_getfield(L, -1, key);  // Get nested field
        lua_remove(L, -2);         // Remove the parent table from stack
    }
    return lua_istable(L, -1);  // Return true if final value is a table
}