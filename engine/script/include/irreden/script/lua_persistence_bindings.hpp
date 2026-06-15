#ifndef LUA_PERSISTENCE_BINDINGS_H
#define LUA_PERSISTENCE_BINDINGS_H

#include <irreden/asset/key_value_store.hpp>
#include <irreden/ir_utility.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace IRScript::detail {

// On-disk path for a named store: <userDataDir("irreden")>/<name>.irkv. The
// per-user data dir (not the exe dir) is what makes a save survive a clean
// reinstall of the game bundle. `saveKeyValueStore` creates the directory.
inline std::string keyValueStorePath(const std::string &name) {
    return IRUtility::joinPath(IRUtility::userDataDir("irreden"), name, "irkv");
}

// Convert a Lua scalar (number / bool / string) to a list element. Any other
// type — including a nested table — throws a sol::error: lists do not nest
// in v1. Lua booleans and numbers are distinct sol types, so there is no
// double-vs-bool ambiguity here.
inline IRAsset::ListElem luaToListElem(const sol::object &obj, const char *context) {
    switch (obj.get_type()) {
    case sol::type::number:
        return IRAsset::ListElem{obj.as<double>()};
    case sol::type::boolean:
        return IRAsset::ListElem{obj.as<bool>()};
    case sol::type::string:
        return IRAsset::ListElem{obj.as<std::string>()};
    default:
        throw sol::error{
            std::string{context} +
            ": list elements must be number, string, or bool (no nested tables)"
        };
    }
}

// Convert a Lua value to an IRAsset::Value. number / bool / string map
// directly; an array-style table (contiguous integer keys 1..n) becomes a
// LIST. A non-array table (string/sparse keys), nil, or function throws a
// sol::error with a clear diagnostic rather than silently no-op'ing.
inline IRAsset::Value luaToValue(const sol::object &obj, const char *context) {
    switch (obj.get_type()) {
    case sol::type::number:
        return IRAsset::Value{obj.as<double>()};
    case sol::type::boolean:
        return IRAsset::Value{obj.as<bool>()};
    case sol::type::string:
        return IRAsset::Value{obj.as<std::string>()};
    case sol::type::table: {
        sol::table tbl = obj.as<sol::table>();
        const std::size_t n = tbl.size(); // array length via the `#` operator
        std::size_t total = 0;
        for (const auto &kv : tbl) {
            (void)kv;
            ++total;
        }
        if (total != n) {
            throw sol::error{
                std::string{context} +
                ": only array-style tables (contiguous 1..n) are supported, not key maps"
            };
        }
        std::vector<IRAsset::ListElem> list;
        list.reserve(n);
        for (std::size_t i = 1; i <= n; ++i) {
            list.push_back(luaToListElem(tbl[i], context));
        }
        return IRAsset::Value{std::move(list)};
    }
    default:
        throw sol::error{
            std::string{context} + ": value must be a number, string, bool, or array table"
        };
    }
}

// Build a Lua object from a list element scalar.
inline sol::object listElemToLua(sol::state_view lua, const IRAsset::ListElem &elem) {
    switch (static_cast<IRAsset::ValueType>(elem.index())) {
    case IRAsset::ValueType::NUMBER:
        return sol::make_object(lua, std::get<double>(elem));
    case IRAsset::ValueType::BOOL:
        return sol::make_object(lua, std::get<bool>(elem));
    case IRAsset::ValueType::STRING:
        return sol::make_object(lua, std::get<std::string>(elem));
    case IRAsset::ValueType::LIST:
        break; // unreachable — ListElem has no LIST alternative
    }
    return sol::make_object(lua, sol::lua_nil);
}

// Build a Lua object from a stored Value. A LIST surfaces as a fresh
// array table allocated in @p lua.
inline sol::object luaFromValue(sol::state_view lua, const IRAsset::Value &value) {
    switch (IRAsset::valueType(value)) {
    case IRAsset::ValueType::NUMBER:
        return sol::make_object(lua, std::get<double>(value));
    case IRAsset::ValueType::BOOL:
        return sol::make_object(lua, std::get<bool>(value));
    case IRAsset::ValueType::STRING:
        return sol::make_object(lua, std::get<std::string>(value));
    case IRAsset::ValueType::LIST: {
        const auto &list = std::get<std::vector<IRAsset::ListElem>>(value);
        sol::table tbl = lua.create_table(static_cast<int>(list.size()), 0);
        for (std::size_t i = 0; i < list.size(); ++i) {
            tbl[i + 1] = listElemToLua(lua, list[i]);
        }
        return tbl;
    }
    }
    return sol::make_object(lua, sol::lua_nil);
}

// Exposes a flat key/value persistence surface as the `IRSave` Lua table
// (engine #1819) so gameplay can save high scores + settings across launches.
// Mirrors the IR<Module> binding convention (IRSim, IRModifier, ...).
//
// Stores live in a per-LuaScript registry keyed by store basename, held in a
// shared_ptr captured by the binding lambdas — so the map lives exactly as
// long as this LuaScript's sol::state (process-registry pattern, like
// IRPrefab::Prefab, but scoped to the World rather than process-global). Don't
// hold Lua handles across World shutdown (script CLAUDE caveat). Files live at
// userDataDir("irreden")/<name>.irkv.
//
// Surface:
//   IRSave.load(name)             -- read file into the in-proc store;
//                                    missing/corrupt -> empty. returns loaded?
//   IRSave.save(name)             -- write in-proc store to disk. returns ok?
//   IRSave.set(name, key, value)  -- value: number | string | bool | array-table
//   IRSave.get(name, key[, def])  -- stored value, or def (nil if omitted)
//   IRSave.has(name, key)
//   IRSave.remove(name, key)      -- returns whether the key existed
//   IRSave.clear(name)
//
// Values cross the boundary by sol::type inspection (number/string/bool/
// array-table), NOT a Lua-spelled enum, so the cpp-lua-enums rule does not
// apply here.
inline void bindPersistenceApi(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["IRSave"].valid()) {
        lua["IRSave"] = lua.create_table();
    }
    sol::table save = lua["IRSave"];

    auto stores = std::make_shared<std::unordered_map<std::string, IRAsset::KeyValueStore>>();

    save["load"] = [stores](const std::string &name) -> bool {
        auto loaded = IRAsset::loadKeyValueStore(keyValueStorePath(name));
        if (!loaded.ok()) {
            (*stores)[name] = IRAsset::KeyValueStore{};
            return false;
        }
        (*stores)[name] = std::move(loaded.value_);
        return true;
    };

    save["save"] = [stores](const std::string &name) -> bool {
        const auto it = stores->find(name);
        if (it == stores->end()) {
            return false;
        }
        return IRAsset::saveKeyValueStore(keyValueStorePath(name), it->second).ok();
    };

    save["set"] = [stores](const std::string &name, const std::string &key, sol::object value) {
        (*stores)[name].set(key, luaToValue(value, "IRSave.set"));
    };

    // sol injects `ts` (this_state) without consuming a Lua arg, so the Lua
    // call is IRSave.get(name, key[, default]).
    save["get"] = [stores](
                      const std::string &name,
                      const std::string &key,
                      sol::object def,
                      sol::this_state ts
                  ) -> sol::object {
        const auto it = stores->find(name);
        if (it != stores->end()) {
            if (const IRAsset::Value *v = it->second.get(key)) {
                return luaFromValue(sol::state_view{ts}, *v);
            }
        }
        return def;
    };

    save["has"] = [stores](const std::string &name, const std::string &key) -> bool {
        const auto it = stores->find(name);
        return it != stores->end() && it->second.has(key);
    };

    save["remove"] = [stores](const std::string &name, const std::string &key) -> bool {
        const auto it = stores->find(name);
        return it != stores->end() && it->second.remove(key);
    };

    save["clear"] = [stores](const std::string &name) { (*stores)[name].clear(); };
}

} // namespace IRScript::detail

#endif /* LUA_PERSISTENCE_BINDINGS_H */
