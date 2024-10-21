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

namespace IRScript {

    // lua_dofile runs a lua script. Global functions and variables
    // can be accessed via the lua stack.
    LuaScript::LuaScript(const char* filename)
    :   lua{}
    {
        IRE_LOG_INFO("Creating new lua script from file: {}", filename);
        IR_ASSERT(filename != NULL, "attemped to create LuaScript object with null file" );
        lua.open_libraries(
            sol::lib::base,
            sol::lib::package,
            sol::lib::string,
            sol::lib::table
        );
        auto result = lua.script_file(filename);
        IR_ASSERT(result.valid(), "Lua script failed to load: {}", filename);

    }

    LuaScript::~LuaScript() {

    }

    sol::table LuaScript::getTable(const char* name) {
        return lua[name];
    }

}