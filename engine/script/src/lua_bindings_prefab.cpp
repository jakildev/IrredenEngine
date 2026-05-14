#include <irreden/script/prefab_api.hpp>

#include <irreden/ir_math.hpp>
#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace IRScript::detail {

void bindPrefabApi(LuaScript &script) {
    sol::state &lua = script.lua();
    if (!lua["Prefab"].valid()) {
        lua["Prefab"] = lua.create_table();
    }

    lua["Prefab"]["register"] = [](const std::string &id, const std::string &path) {
        IRPrefab::Prefab::registerPrefab(id, path);
    };

    // Accepts vec3 or {x,y,z}/{1,2,3} table; returns LuaEntity+nil on success, nil+error on failure.
    lua["Prefab"]["spawn"] = [&script](
                                 const std::string &id, sol::object positionObj
                             ) -> std::tuple<sol::object, sol::object> {
        sol::state_view sv{script.lua().lua_state()};
        if (positionObj.valid() && positionObj.get_type() != sol::type::lua_nil &&
            positionObj.get_type() != sol::type::none && !positionObj.is<IRMath::vec3>() &&
            !positionObj.is<sol::table>()) {
            return {sol::make_object(sv, sol::lua_nil),
                    sol::make_object(
                        sv, std::string{"Prefab.spawn: position must be a vec3 or {x,y,z} table"}
                    )};
        }
        IRMath::vec3 position = IRScript::vec3FromLua(positionObj);

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
