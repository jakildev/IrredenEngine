#include <irreden/script/prefab_api.hpp>

#include <irreden/ir_math.hpp>
#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace IRScript::detail {

/// Wires `Prefab.register(id, path)` and `Prefab.spawn(id, position)`
/// into `script`'s Lua state. The bound closures forward into the
/// `IRPrefab::Prefab` C++ API, so the Lua surface stays a thin shim
/// over the typed entry points.
void bindPrefabApi(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["Prefab"].valid()) {
        lua["Prefab"] = lua.create_table();
    }

    lua["Prefab"]["register"] = [](const std::string &id, const std::string &path) {
        IRPrefab::Prefab::registerPrefab(id, path);
    };

    // `Prefab.spawn(id, position)` — accepts the position as either an
    // `IRMath::vec3` userdata or a `{x, y, z}` table for ergonomics
    // mirroring `IREntity.create*` patterns. Returns a `LuaEntity`
    // table on success and `nil` on failure (the error is logged at
    // C++-side ERROR level and surfaced via the second return value).
    lua["Prefab"]["spawn"] = [&script](
                                 const std::string &id, sol::object positionObj
                             ) -> std::tuple<sol::object, sol::object> {
        sol::state_view sv{script.lua().lua_state()};
        IRMath::vec3 position{0.0f, 0.0f, 0.0f};
        if (positionObj.is<IRMath::vec3>()) {
            position = positionObj.as<IRMath::vec3>();
        } else if (positionObj.is<sol::table>()) {
            sol::table t = positionObj.as<sol::table>();
            // Accept either `{x=…, y=…, z=…}` or `{1, 2, 3}`. Use a
            // helper to keep sol2's overload set unambiguous —
            // `get_or<T>` with mixed key types confuses overload
            // resolution.
            auto pickFloat = [&t](const char *key, int idx) -> float {
                sol::optional<float> byName = t[key];
                if (byName)
                    return *byName;
                sol::optional<float> byIndex = t[idx];
                if (byIndex)
                    return *byIndex;
                return 0.0f;
            };
            position.x = pickFloat("x", 1);
            position.y = pickFloat("y", 2);
            position.z = pickFloat("z", 3);
        } else if (positionObj.valid() && positionObj.get_type() != sol::type::lua_nil &&
                   positionObj.get_type() != sol::type::none) {
            return {sol::make_object(sv, sol::lua_nil),
                    sol::make_object(
                        sv, std::string{"Prefab.spawn: position must be a vec3 or {x,y,z} table"}
                    )};
        }

        IRPrefab::Prefab::SpawnResult result =
            IRPrefab::Prefab::spawnPrefab(script, id, position);
        if (result.entity_ == IREntity::kNullEntity) {
            return {sol::make_object(sv, sol::lua_nil), sol::make_object(sv, result.error_)};
        }
        return {sol::make_object(sv, IRScript::LuaEntity{result.entity_}),
                sol::make_object(sv, sol::lua_nil)};
    };
}

} // namespace IRScript::detail
