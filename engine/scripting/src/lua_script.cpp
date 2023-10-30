/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\lua\lua_script.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include "lua_script.hpp"
#include <irreden/ir_profiling.hpp>

// lua_dofile runs a lua script. Global functions and variables
// can be accessed via the lua stack.
LuaScript::LuaScript(const char* filename, bool withLibs) {
    IRProfile::engLogInfo("Creating new lua script from file: {}", filename);
    L = luaL_newstate();
    IRProfile::engAssert(filename != NULL, "attemped to create LuaScript object with null file" );
    if (withLibs) {
        luaL_openlibs(L);
    }
    int status = luaL_dofile(L, filename);
    IRProfile::engAssert(status == LUA_OK, "luaL_dofile failed with error");
}


LuaScript::~LuaScript() {
    if(L) lua_close(L);
}