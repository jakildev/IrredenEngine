#ifndef LUA_CONFIG_H
#define LUA_CONFIG_H

#include <irreden/script/ir_script_types.hpp>

#include <irreden/script/lua_value.hpp>

namespace IRScript {

struct LuaConfigEntry {
    std::string luaName;
    std::unique_ptr<ILuaValue> value;

    LuaConfigEntry(const std::string &name, std::unique_ptr<ILuaValue> val)
        : luaName(name)
        , value(std::move(val)) {}
};

class LuaConfig {
  public:
    std::map<std::string, std::unique_ptr<ILuaValue>> entries;

    void addEntry(const std::string &luaName, std::unique_ptr<ILuaValue> value) {
        entries[luaName] = std::move(value);
    }

    // Parse the Lua table
    void parse(sol::table luaTable) {
        for (auto &entry : entries) {
            sol::object luaValue = luaTable[entry.first];
            if (luaValue.valid()) {
                entry.second->parse(luaValue); // Parse the Lua value
            } else {
                entry.second->reset_to_default(); // Use the default value if missing
            }
        }
    }

    ILuaValue &operator[](const std::string &key) {
        auto it = entries.find(key);
        if (it != entries.end()) {
            return *(it->second);
        } else {
            throw std::runtime_error("Key not found: " + key);
        }
    }
};

} // namespace IRScript

#endif /* LUA_CONFIG_H */
