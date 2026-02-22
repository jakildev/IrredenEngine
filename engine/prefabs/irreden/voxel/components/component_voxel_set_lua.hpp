#ifndef COMPONENT_VOXEL_SET_LUA_H
#define COMPONENT_VOXEL_SET_LUA_H

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_VoxelSetNew> = true;

template <> inline void bindLuaType<IRComponents::C_VoxelSetNew>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_VoxelSetNew,
        IRComponents::C_VoxelSetNew(IRMath::ivec3, IRMath::Color)>("C_VoxelSetNew");
}
} // namespace IRScript

#endif /* COMPONENT_VOXEL_SET_LUA_H */
