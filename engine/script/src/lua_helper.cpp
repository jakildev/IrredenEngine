/*
 * Project: Irreden Engine
 * File: lua_helper.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_profile.hpp>

#include <irreden/script/lua_helper.hpp>

#include <string>

bool checkLua(lua_State *L, const int r) {
    if (r != LUA_OK) {
        std::string errormsg = lua_tostring(L, -1);
        IRE_LOG_ERROR("{}", errormsg);
        return false;
    }
    return true;
}