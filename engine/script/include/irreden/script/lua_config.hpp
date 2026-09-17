#ifndef LUA_CONFIG_H
#define LUA_CONFIG_H

#include <irreden/script/ir_script_types.hpp>

#include <irreden/script/lua_value.hpp>

namespace IRScript {

/// A single named entry in a `LuaConfig`: maps a Lua field name to a typed
/// `ILuaValue` that owns the stored value and its default.
struct LuaConfigEntry {
    std::string luaName;
    std::unique_ptr<ILuaValue> value;

    LuaConfigEntry(const std::string &name, std::unique_ptr<ILuaValue> val)
        : luaName(name)
        , value(std::move(val)) {}
};

/// String-keyed collection of typed Lua configuration values.
/// Populate with `addEntry()`, then call `parse(luaTable)` to populate all
/// values from a `sol::table`; missing keys fall back to their defaults.
/// `overlay(luaTable)` layers a second table on top, touching only the keys
/// it carries. Use `operator[]` to retrieve individual values after parsing.
class LuaConfig {
  public:
    std::map<std::string, std::unique_ptr<ILuaValue>> entries;

    /// Adds or replaces the entry for @p luaName with the given typed value.
    void addEntry(const std::string &luaName, std::unique_ptr<ILuaValue> value) {
        entries[luaName] = std::move(value);
    }

    /// Parses all registered entries from @p luaTable, falling back to each
    /// entry's default when a key is absent.
    void parse(sol::table luaTable) {
        apply(luaTable, true);
    }

    /// Re-parses only the registered entries present in @p luaTable, leaving
    /// every absent key at its current value — the layering counterpart of
    /// @c parse.
    void overlay(sol::table luaTable) {
        apply(luaTable, false);
    }

    /// Returns a reference to the value for @p key.  Throws if the key is absent.
    ILuaValue &operator[](const std::string &key) {
        auto it = entries.find(key);
        if (it != entries.end()) {
            return *(it->second);
        } else {
            throw std::runtime_error("Key not found: " + key);
        }
    }

  private:
    void apply(sol::table luaTable, bool resetMissing) {
        for (auto &entry : entries) {
            sol::object luaValue = luaTable[entry.first];
            if (luaValue.valid()) {
                entry.second->parse(luaValue);
            } else if (resetMissing) {
                entry.second->reset_to_default();
            }
        }
    }
};

} // namespace IRScript

#endif /* LUA_CONFIG_H */
