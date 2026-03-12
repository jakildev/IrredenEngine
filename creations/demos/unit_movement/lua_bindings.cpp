#include "lua_bindings.hpp"
#include "lua_component_pack.hpp"

#include <irreden/script/lua_script.hpp>
#include <irreden/input/systems/system_entity_hover_detect.hpp>
#include <irreden/input/systems/system_unit_selection.hpp>
#include <irreden/ir_engine.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_selected.hpp>
#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/components/component_nav_cell.hpp>
#include <irreden/update/components/component_nav_world.hpp>
#include <irreden/update/components/component_chunk_registry.hpp>
#include <irreden/update/components/component_flow_field.hpp>
#include <irreden/update/components/component_flow_field_request.hpp>
#include <irreden/update/nav_query.hpp>
#include <irreden/update/components/component_move_order.hpp>

#include <cmath>
#include <vector>

namespace UnitMovement {

void registerLuaBindings() {
    static bool isRegistered = false;
    if (isRegistered) return;
    isRegistered = true;

    IREngine::registerLuaBindings([](IRScript::LuaScript &luaScript) {
        using namespace IRMath;
        using namespace IRComponents;

        UnitMovement::registerLuaComponentPack(luaScript);

        luaScript.registerType<ivec3, ivec3(int, int, int)>(
            "ivec3", "x", &ivec3::x, "y", &ivec3::y, "z", &ivec3::z);
        luaScript.lua()["ivec3"]["new"] = [](int x, int y, int z) { return ivec3(x, y, z); };

        luaScript.registerType<vec3, vec3(float, float, float)>(
            "vec3", "x", &vec3::x, "y", &vec3::y, "z", &vec3::z);
        luaScript.lua()["vec3"]["new"] = [](float x, float y, float z) { return vec3(x, y, z); };

        luaScript.registerType<Color, Color(int, int, int, int)>(
            "Color", "r", &Color::red_, "g", &Color::green_, "b", &Color::blue_, "a", &Color::alpha_);
        luaScript.lua()["Color"]["new"] = [](int r, int g, int b, int a) {
            return Color(r, g, b, a);
        };

        luaScript.lua()["IRCollisionLayer"] = luaScript.lua().create_table();
        luaScript.lua()["IRCollisionLayer"]["DEFAULT"] = COLLISION_LAYER_DEFAULT;
        luaScript.lua()["IRCollisionLayer"]["NOTE_BLOCK"] = COLLISION_LAYER_NOTE_BLOCK;
        luaScript.lua()["IRCollisionLayer"]["NOTE_PLATFORM"] = COLLISION_LAYER_NOTE_PLATFORM;
        luaScript.lua()["IRCollisionLayer"]["PARTICLE"] = COLLISION_LAYER_PARTICLE;

        luaScript.lua()["IREntity"] = luaScript.lua().create_table();
        luaScript.lua()["IREntity"]["destroyEntity"] = [](IRScript::LuaEntity handle) {
            IREntity::destroyEntity(handle.entity);
        };
        luaScript.registerType<
            IREntity::CreateEntityCallbackParams,
            IREntity::CreateEntityCallbackParams(ivec3, vec3)>(
            "CreateEntityCallbackParams",
            "center",
            &IREntity::CreateEntityCallbackParams::center,
            "index",
            &IREntity::CreateEntityCallbackParams::index);
        luaScript.lua()["IREntity"]["getEntity"] = [](const std::string &name) {
            return IRScript::LuaEntity{IREntity::getEntity(name)};
        };
        luaScript.lua()["IREntity"]["setPosition"] =
            [](IRScript::LuaEntity handle, float x, float y, float z) {
                IREntity::getComponent<C_Position3D>(handle.entity).pos_ = vec3(x, y, z);
            };

        luaScript.lua()["IREntity"]["createLevelEntity"] = [](float cellSize, sol::optional<std::string> nameOpt) {
            std::string levelName = nameOpt.value_or("level");
            IREntity::EntityId entity = IREntity::createEntity(
                C_NavWorld(cellSize),
                C_ChunkRegistry(),
                C_FlowFieldRequest(),
                C_FlowField(),
                C_Name(levelName)
            );
            return IRScript::LuaEntity{entity};
        };
        luaScript.lua()["IREntity"]["createLevelEntityFromVoxelConfig"] =
            [](float voxelSizeWorld, int voxelsPerNavCell, sol::optional<float> playerRadiusVoxelsOpt,
               sol::optional<std::string> nameOpt) {
                float playerRadius = playerRadiusVoxelsOpt.value_or(7.0f);
                std::string levelName = nameOpt.value_or("level");
                C_NavWorld navWorld = C_NavWorld::fromVoxelConfig(
                    voxelSizeWorld, voxelsPerNavCell, playerRadius);
                IREntity::EntityId entity = IREntity::createEntity(
                    navWorld,
                    C_ChunkRegistry(),
                    C_FlowFieldRequest(),
                    C_FlowField(),
                    C_Name(levelName)
                );
                return IRScript::LuaEntity{entity};
            };
        luaScript.lua()["IREntity"]["setNavGridOrigin"] =
            [](IRScript::LuaEntity levelEntity, int sizeX, int sizeY,
               sol::optional<float> cellSizeOpt, sol::optional<int> chunkSizeOpt) {
                auto &navWorld = IREntity::getComponent<C_NavWorld>(levelEntity.entity);
                float cellSize = cellSizeOpt.value_or(navWorld.cellSizeWorld_);
                float ox = -std::floor(sizeX / 2.f) * cellSize;
                float oy = -std::floor(sizeY / 2.f) * cellSize;
                navWorld.origin_ = vec3(ox, oy, 0.0f);
                int cs = chunkSizeOpt.value_or(sizeX);
                navWorld.chunkSize_ = ivec3(cs, cs, 1);
            };
        luaScript.lua()["IREntity"]["getNavCellSize"] = [](IRScript::LuaEntity levelEntity) {
            return IREntity::getComponent<C_NavWorld>(levelEntity.entity).cellSizeWorld_;
        };
        luaScript.lua()["IREntity"]["getNavDefaultAgentClearance"] = [](IRScript::LuaEntity levelEntity) {
            return IREntity::getComponent<C_NavWorld>(levelEntity.entity).getDefaultAgentClearanceWorld();
        };

        luaScript.registerCreateEntityBatchFunction<
            C_NavCell,
            C_Position3D,
            C_VoxelSetNew>("createEntityBatchNavCells");

        luaScript.registerCreateEntityBatchFunction<
            C_Position3D,
            C_ControllableUnit,
            C_NavAgent,
            C_ColliderIso3DAABB,
            C_ColliderCircle,
            C_Facing2D,
            C_SmoothMovement,
            C_CollisionLayer,
            C_VoxelSetNew>("createEntityBatchUnits");

        luaScript.lua()["IRRender"] = luaScript.lua().create_table();
        luaScript.lua()["IRRender"]["setCameraZoom"] = [](float z) { IRRender::setCameraZoom(z); };
        luaScript.lua()["IRRender"]["setCameraPosition2DIso"] = [](float x, float y) {
            IRRender::setCameraPosition2DIso(vec2(x, y));
        };

        luaScript.lua()["IRInput"] = luaScript.lua().create_table();
        luaScript.lua()["IRInput"]["onEntityClicked"] = [](sol::protected_function fn) {
            return IRSystem::getEntityEventHandlers().addOnClicked(std::move(fn));
        };
        luaScript.lua()["IRInput"]["onRightClick"] = [](sol::protected_function fn) {
            return IRSystem::getEntityEventHandlers().addOnRightClick(std::move(fn));
        };
        luaScript.lua()["IRInput"]["onLeftClick"] = [](sol::protected_function fn) {
            return IRSystem::getEntityEventHandlers().addOnClicked(std::move(fn));
        };

        luaScript.lua()["IREntity"]["getNavCellGridPos"] =
            [](IREntity::EntityId eid, sol::this_state L) -> sol::object {
            auto opt = IREntity::getComponentOptional<C_NavCell>(eid);
            if (!opt) return sol::make_object(L, sol::lua_nil);
            return sol::make_object(L, (*opt)->gridPos_);
        };
        luaScript.lua()["IREntity"]["getNavCellAtMouseWorldIso"] =
            [](sol::this_state L) -> sol::object {
            vec2 iso = IRRender::mousePosition2DIsoWorldRender();
            float x = -(iso.x + iso.y) * 0.5f;
            float y = (iso.x - iso.y) * 0.5f;
            vec3 worldPos(x, y, 0.0f);

            auto nodes = IREntity::queryArchetypeNodesSimple(
                IREntity::getArchetype<C_NavWorld, C_ChunkRegistry>()
            );
            if (nodes.empty()) return sol::make_object(L, sol::lua_nil);

            IREntity::EntityId levelEntityId = nodes[0]->entities_[0];
            ivec3 cell = IRMath::navWorldToCell(levelEntityId, worldPos);
            if (!IRMath::navCellExists(levelEntityId, cell)) return sol::make_object(L, sol::lua_nil);
            return sol::make_object(L, cell);
        };

        // Issue move order to all controllable units (fallback)
        luaScript.lua()["IREntity"]["issueMoveOrderToAllUnits"] = [](ivec3 targetCell) {
            std::vector<IREntity::EntityId> entityIds;
            auto nodes = IREntity::queryArchetypeNodesSimple(
                IREntity::getArchetype<C_ControllableUnit, C_NavAgent>()
            );
            for (auto *node : nodes) {
                for (int i = 0; i < node->length_; i++) {
                    entityIds.push_back(node->entities_[i]);
                }
            }
            for (auto entityId : entityIds) {
                IREntity::setComponent(entityId, C_MoveOrder(targetCell));
            }
        };

        // Issue move order only to selected units
        luaScript.lua()["IREntity"]["issueMoveOrderToSelectedUnits"] = [](ivec3 targetCell) {
            const int requestedSelectedCount = IRSystem::UnitSelection::countSelectedUnits();
            std::vector<IREntity::EntityId> entityIds;
            auto nodes = IREntity::queryArchetypeNodesSimple(
                IREntity::getArchetype<C_Selected, C_ControllableUnit, C_NavAgent>()
            );
            int recipientCount = 0;
            for (auto *node : nodes) {
                for (int i = 0; i < node->length_; i++) {
                    entityIds.push_back(node->entities_[i]);
                }
            }
            for (auto entityId : entityIds) {
                IREntity::setComponent(entityId, C_MoveOrder(targetCell));
                recipientCount++;
            }
            IRSystem::UnitSelection::recordMoveOrderDebug(
                targetCell,
                requestedSelectedCount,
                recipientCount,
                false
            );
        };

        // Select a single unit (deselects others)
        luaScript.lua()["IREntity"]["selectUnit"] = [](IREntity::EntityId entityId) {
            // Deselect all
            std::vector<IREntity::EntityId> selectedIds;
            auto nodes = IREntity::queryArchetypeNodesSimple(
                IREntity::getArchetype<C_Selected, C_ControllableUnit>()
            );
            for (auto *node : nodes) {
                for (int i = 0; i < node->length_; i++) {
                    selectedIds.push_back(node->entities_[i]);
                }
            }
            for (auto selectedId : selectedIds) {
                IREntity::removeComponent<C_Selected>(selectedId);
            }
            // Select target
            auto opt = IREntity::getComponentOptional<C_ControllableUnit>(entityId);
            if (opt) {
                IREntity::setComponent(entityId, C_Selected{});
            }
        };

        // Select all controllable units
        luaScript.lua()["IREntity"]["selectAllUnits"] = []() {
            std::vector<IREntity::EntityId> entityIds;
            auto nodes = IREntity::queryArchetypeNodesSimple(
                IREntity::getArchetype<C_ControllableUnit>()
            );
            for (auto *node : nodes) {
                for (int i = 0; i < node->length_; i++) {
                    entityIds.push_back(node->entities_[i]);
                }
            }
            for (auto entityId : entityIds) {
                IREntity::setComponent(entityId, C_Selected{});
            }
        };

        // Deselect all units
        luaScript.lua()["IREntity"]["deselectAllUnits"] = []() {
            std::vector<IREntity::EntityId> entityIds;
            auto nodes = IREntity::queryArchetypeNodesSimple(
                IREntity::getArchetype<C_Selected, C_ControllableUnit>()
            );
            for (auto *node : nodes) {
                for (int i = 0; i < node->length_; i++) {
                    entityIds.push_back(node->entities_[i]);
                }
            }
            for (auto entityId : entityIds) {
                IREntity::removeComponent<C_Selected>(entityId);
            }
        };

        // Check if an entity is a controllable unit
        luaScript.lua()["IREntity"]["isControllableUnit"] = [](IREntity::EntityId entityId) -> bool {
            return IREntity::getComponentOptional<C_ControllableUnit>(entityId).has_value();
        };
    });
}

} // namespace UnitMovement
