#ifndef COMPONENT_NAV_AGENT_LUA_H
#define COMPONENT_NAV_AGENT_LUA_H

#include <irreden/update/components/component_nav_agent.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_NavAgent> = true;

template <> inline void bindLuaType<IRComponents::C_NavAgent>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_NavAgent,
        IRComponents::C_NavAgent(float),
        IRComponents::C_NavAgent()>(
        "C_NavAgent",
        "moveSpeed",
        &IRComponents::C_NavAgent::moveSpeed_,
        "agentClearance",
        &IRComponents::C_NavAgent::agentClearance_
    );
}
} // namespace IRScript

#endif /* COMPONENT_NAV_AGENT_LUA_H */
