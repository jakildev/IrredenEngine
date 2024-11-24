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
    LuaScript::LuaScript()
    :   m_lua{}
    {
        m_lua.open_libraries(
            sol::lib::base,
            sol::lib::package,
            sol::lib::string,
            sol::lib::table,
            sol::lib::math
        );

    }

    LuaScript::LuaScript(const char* filename)
    :   LuaScript{}
    {

        scriptFile(filename);
    }

    LuaScript::~LuaScript() {

    }

    void LuaScript::scriptFile(const char * filename) {
        IRE_LOG_INFO("Creating new Lua script from file: {}", filename);

    // Ensure filename is not NULL
    IR_ASSERT(filename != nullptr, "Attempted to create LuaScript object with null file");

    try {
        // Execute the Lua script file in a protected way
        sol::protected_function_result result = m_lua.script_file(filename);
        // sol::protected_function_result result = m_lua.safe_script(
        //     filename,
        //     &sol::script_pass_on_error
        // );

        if (!result.valid()) {
            sol::error err = result;
            IRE_LOG_ERROR("Lua script failed to load: {}. Error: {}", filename, err.what());
            return;
        }

        IRE_LOG_INFO("Lua script loaded successfully: {}", filename);
    }
    catch (const sol::error& e) {
        // Catch and log any exception thrown by Sol2
        IRE_LOG_ERROR("Exception during Lua script loading: {}", e.what());
    }
    catch (const std::exception& e) {
        // Catch any other standard exception and log it
        IRE_LOG_ERROR("Standard exception during Lua script loading: {}", e.what());
    }
    }

    sol::table LuaScript::getTable(const char* name) {
        return m_lua[name];
    }

}