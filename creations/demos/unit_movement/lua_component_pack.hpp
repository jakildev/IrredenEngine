#ifndef UNIT_MOVEMENT_LUA_COMPONENT_PACK_H
#define UNIT_MOVEMENT_LUA_COMPONENT_PACK_H

#include <irreden/common/components/component_position_3d_lua.hpp>
#include <irreden/common/components/component_controllable_unit_lua.hpp>
#include <irreden/common/components/component_selected_lua.hpp>
#include <irreden/voxel/components/component_voxel_set_lua.hpp>
#include <irreden/update/components/component_nav_cell_lua.hpp>
#include <irreden/update/components/component_nav_agent_lua.hpp>
#include <irreden/update/components/component_move_order_lua.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb_lua.hpp>
#include <irreden/update/components/component_collider_circle_lua.hpp>
#include <irreden/update/components/component_facing_2d_lua.hpp>
#include <irreden/common/components/component_smooth_movement_lua.hpp>
#include <irreden/update/components/component_collision_layer_lua.hpp>
#include <irreden/update/components/component_collision_layer.hpp>

namespace UnitMovement {
inline void registerLuaComponentPack(IRScript::LuaScript &luaScript) {
    using namespace IRComponents;
    luaScript.registerTypesFromTraits<
        C_Position3D,
        C_ControllableUnit,
        C_Selected,
        C_VoxelSetNew,
        C_NavCell,
        C_NavAgent,
        C_MoveOrder,
        C_ColliderIso3DAABB,
        C_ColliderCircle,
        C_Facing2D,
        C_SmoothMovement,
        C_CollisionLayer>();
}
} // namespace UnitMovement

#endif /* UNIT_MOVEMENT_LUA_COMPONENT_PACK_H */
