/*
 * Project: Irreden Engine
 * File: \irreden-engine\engine\src\lua\lua_helper.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include "lua_helper.hpp"
#include <string>
#include <irreden/ir_profiling.hpp>

bool checkLua(lua_State *L, const int r) {
    if (r != LUA_OK) {
        std::string errormsg = lua_tostring(L, -1);
        IRProfile::engLogError("{}", errormsg);
        return false;
    }
    return true;
}