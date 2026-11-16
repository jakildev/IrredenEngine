#ifndef COMPONENT_STATIC_LUA_H
#define COMPONENT_STATIC_LUA_H

#include <irreden/common/static_residents.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/script/prefab_component_factory.hpp>
#include <irreden/system/components/component_static.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_Static> = true;

// Binds the static-resident surface (docs/design/static-residents.md):
// `C_Static` for a prefab's `components = { C_Static = {} }` and for a Lua
// system's component lists, and the `IRStatic` table.
//
//   IRStatic.freeze(entity)   -> an IRStatic.FreezeResult value
//   IRStatic.thaw(entity)
//   IRStatic.resettle(entity)
//   IRStatic.isStatic(entity) / IRStatic.isSettled(entity)
//   IRStatic.stats()          -> populations and transition counters
template <> inline void bindLuaType<IRComponents::C_Static>(LuaScript &luaScript) {
    using IRComponents::C_Static;
    luaScript.registerType<C_Static, C_Static()>("C_Static");
    registerComponentFactoryFor<C_Static>("C_Static", [](C_Static &, const sol::table &) {});

    sol::state &lua = luaScript.lua();
    sol::table table = lua.create_table();
    sol::table freezeResult = lua.create_table();
#define IR_BIND_FREEZE_RESULT(name)                                                                \
    freezeResult[#name] = static_cast<lua_Integer>(IRPrefab::Static::FreezeResult::name)
    IR_BIND_FREEZE_RESULT(FROZEN);
    IR_BIND_FREEZE_RESULT(ALREADY_STATIC);
    IR_BIND_FREEZE_RESULT(MISSING);
    IR_BIND_FREEZE_RESULT(PARENT_NOT_STATIC);
    IR_BIND_FREEZE_RESULT(NOT_GRID);
    IR_BIND_FREEZE_RESULT(GPU_TRANSFORM);
    IR_BIND_FREEZE_RESULT(SKELETAL);
#undef IR_BIND_FREEZE_RESULT
    table["FreezeResult"] = freezeResult;
    table["freeze"] = [](lua_Integer entity) {
        return static_cast<lua_Integer>(
            IRPrefab::Static::freeze(static_cast<IREntity::EntityId>(entity))
        );
    };
    table["thaw"] = [](lua_Integer entity) {
        IRPrefab::Static::thaw(static_cast<IREntity::EntityId>(entity));
    };
    table["resettle"] = [](lua_Integer entity) {
        IRPrefab::Static::resettle(static_cast<IREntity::EntityId>(entity));
    };
    table["isStatic"] = [](lua_Integer entity) {
        return IRPrefab::Static::isStatic(static_cast<IREntity::EntityId>(entity));
    };
    table["isSettled"] = [](lua_Integer entity) {
        return IRPrefab::Static::isSettled(static_cast<IREntity::EntityId>(entity));
    };
    table["stats"] = [](sol::this_state ts) {
        const IRPrefab::Static::Stats stats = IRPrefab::Static::stats();
        sol::table out = sol::state_view{ts}.create_table();
        out["static"] = static_cast<lua_Integer>(stats.static_);
        out["settling"] = static_cast<lua_Integer>(stats.settling_);
        out["settled"] = static_cast<lua_Integer>(stats.settled_);
        out["promoted"] = static_cast<lua_Integer>(stats.counters_.promoted_);
        out["thawed"] = static_cast<lua_Integer>(stats.counters_.thawed_);
        out["resettled"] = static_cast<lua_Integer>(stats.counters_.resettled_);
        out["restarted"] = static_cast<lua_Integer>(stats.counters_.restarted_);
        out["held"] = static_cast<lua_Integer>(stats.counters_.held_);
        out["heldByGlobal"] = static_cast<lua_Integer>(stats.counters_.heldByGlobal_);
        out["refused"] = static_cast<lua_Integer>(stats.counters_.refused_);
        return out;
    };
    lua["IRStatic"] = table;
}
} // namespace IRScript

#endif /* COMPONENT_STATIC_LUA_H */
