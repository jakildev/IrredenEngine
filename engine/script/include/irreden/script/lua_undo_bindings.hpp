#ifndef LUA_UNDO_BINDINGS_H
#define LUA_UNDO_BINDINGS_H

#include <irreden/common/undo.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <stdexcept>
#include <string>
#include <utility>

namespace IRScript::detail {

inline std::string requireUndoString(const sol::table &record, const char *field) {
    sol::object value = record[field];
    if (value.get_type() != sol::type::string) {
        throw std::invalid_argument(std::string("IRUndo.push requires string '") + field + "'");
    }
    return value.as<std::string>();
}

inline sol::protected_function requireUndoFunction(const sol::table &record, const char *field) {
    sol::object value = record[field];
    if (value.get_type() != sol::type::function) {
        throw std::invalid_argument(std::string("IRUndo.push requires function '") + field + "'");
    }
    return value.as<sol::protected_function>();
}

inline std::function<void()> protectUndoFunction(sol::protected_function fn, std::string label) {
    return [fn = std::move(fn), label = std::move(label)]() mutable {
        sol::protected_function_result result = fn();
        if (!result.valid()) {
            sol::error error = result;
            IRE_LOG_ERROR("IRUndo {} callback error: {}", label, error.what());
        }
    };
}

inline void bindUndo(LuaScript &script) {
    IRPrefab::Undo::materialize();

    sol::state &lua = script.lua();
    sol::object existing = lua["IRUndo"];
    if (existing.valid() && existing.get_type() != sol::type::lua_nil &&
        existing.get_type() != sol::type::table) {
        throw std::invalid_argument("IRUndo must be a table before bindLuaUndo()");
    }
    sol::table undo =
        existing.get_type() == sol::type::table ? existing.as<sol::table>() : lua.create_table();

    undo["push"] = [](sol::table value) {
        std::string label = requireUndoString(value, "label");
        sol::protected_function undoFn = requireUndoFunction(value, "undo");
        sol::protected_function redoFn = requireUndoFunction(value, "redo");
        std::string coalesce;
        sol::object coalesceValue = value["coalesce"];
        if (coalesceValue.valid() && coalesceValue.get_type() != sol::type::lua_nil) {
            if (coalesceValue.get_type() != sol::type::string) {
                throw std::invalid_argument("IRUndo.push field 'coalesce' must be a string");
            }
            coalesce = coalesceValue.as<std::string>();
        }
        return IRPrefab::Undo::push({
            label,
            protectUndoFunction(std::move(undoFn), label + " undo"),
            protectUndoFunction(std::move(redoFn), label + " redo"),
            std::move(coalesce),
        });
    };
    undo["undo"] = []() { return IRPrefab::Undo::undo(); };
    undo["redo"] = []() { return IRPrefab::Undo::redo(); };
    undo["canUndo"] = []() { return IRPrefab::Undo::canUndo(); };
    undo["canRedo"] = []() { return IRPrefab::Undo::canRedo(); };
    undo["clear"] = []() { IRPrefab::Undo::clear(); };

    lua["IRUndo"] = undo;
}

} // namespace IRScript::detail

#endif /* LUA_UNDO_BINDINGS_H */
