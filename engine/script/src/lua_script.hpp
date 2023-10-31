/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\lua\lua_script.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef LUA_SCRIPT_H
#define LUA_SCRIPT_H

#include <lua54/lua.hpp> // hpp file contains the extern "C" directive

class LuaScript {
public:
    LuaScript(const char* filename, bool withLibs = true);
    inline lua_State* getState() { return L; }
    ~LuaScript();
private:
    lua_State *L;
};

#endif