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

namespace IRScript {

    class LuaScript {
    public:
        LuaScript(const char* filename);

        ~LuaScript();

        sol::table getTable(const char* name);
    private:
        sol::state lua;
    };
} // namespace IRScript

#endif /* LUA_SCRIPT_H */
