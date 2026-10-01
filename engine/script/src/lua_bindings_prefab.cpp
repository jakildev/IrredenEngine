#include <irreden/script/prefab_api.hpp>

#include <irreden/ir_math.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/script/ir_script_types.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <sol/sol.hpp>

#include <string>
#include <utility>

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
            positionObj.get_type() != sol::type::table) {
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

    if (!lua["IRPrefab"].valid()) {
        lua["IRPrefab"] = lua.create_table();
    }
    lua["IRPrefab"]["setRotationMode"] = [](IRScript::LuaEntity entity,
                                            sol::object modeObject,
                                            sol::optional<sol::table> optionsTable) {
        if (!modeObject.is<lua_Integer>()) {
            throw sol::error{
                "IRPrefab.setRotationMode: mode must be an IRComponent.RotationMode value"
            };
        }
        const lua_Integer rawMode = modeObject.as<lua_Integer>();
        if (rawMode < static_cast<lua_Integer>(IRComponents::RotationMode::kFirst) ||
            rawMode > static_cast<lua_Integer>(IRComponents::RotationMode::kLast)) {
            throw sol::error{"IRPrefab.setRotationMode: mode is outside IRComponent.RotationMode"};
        }

        IRPrefab::RotationMode::SetModeOptions options;
        if (optionsTable) {
            if (sol::optional<std::string> name = (*optionsTable)["canvas_name"]; name) {
                options.canvasName_ = *name;
            }
            if (sol::optional<sol::table> size = (*optionsTable)["canvas_size"]; size) {
                const sol::optional<int> width = (*size)["x"];
                const sol::optional<int> height = (*size)["y"];
                if (!width || !height || *width <= 0 || *height <= 0) {
                    throw sol::error{
                        "IRPrefab.setRotationMode: canvas_size requires positive integer x and y"
                    };
                }
                options.canvasSize_ = IRMath::ivec2{*width, *height};
            }
            if (sol::optional<bool> screenLocked = (*optionsTable)["screen_locked"]; screenLocked) {
                options.screenLocked_ = *screenLocked;
            }
            if (sol::optional<int> depthPriority = (*optionsTable)["depth_priority"];
                depthPriority) {
                options.depthPriority_ = *depthPriority;
            }
        }

        const auto mode = static_cast<IRComponents::RotationMode>(rawMode);
        const bool needsCanvas =
            IRPrefab::RotationMode::ownsEntityCanvas(mode) &&
            !IREntity::getComponentOptional<IRComponents::C_EntityCanvas>(entity.entity);
        if (needsCanvas) {
            auto voxelSet =
                IREntity::getComponentOptional<IRComponents::C_VoxelSetNew>(entity.entity);
            if (!voxelSet || voxelSet.value()->recordCount() == 0) {
                throw sol::error{
                    "IRPrefab.setRotationMode: detached modes require a non-empty C_VoxelSetNew"
                };
            }
        }
        IREntity::getEntityManager().stageStructuralChange(
            [entity = entity.entity, mode, options = std::move(options)]() mutable {
                IRPrefab::RotationMode::setMode(entity, mode, std::move(options));
            }
        );
    };
}

} // namespace IRScript::detail
