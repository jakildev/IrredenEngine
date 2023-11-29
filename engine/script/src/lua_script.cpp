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