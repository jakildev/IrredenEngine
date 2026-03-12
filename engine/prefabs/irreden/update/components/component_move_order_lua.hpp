#ifndef COMPONENT_MOVE_ORDER_LUA_H
#define COMPONENT_MOVE_ORDER_LUA_H

#include <irreden/update/components/component_move_order.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_MoveOrder> = true;

template <> inline void bindLuaType<IRComponents::C_MoveOrder>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_MoveOrder,
        IRComponents::C_MoveOrder(IRMath::ivec3),
        IRComponents::C_MoveOrder()>(
        "C_MoveOrder",
        "targetCell",
        &IRComponents::C_MoveOrder::targetCell_
    );
}
} // namespace IRScript

#endif /* COMPONENT_MOVE_ORDER_LUA_H */
