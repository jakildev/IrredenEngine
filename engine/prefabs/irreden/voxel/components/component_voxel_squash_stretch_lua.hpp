#ifndef COMPONENT_VOXEL_SQUASH_STRETCH_LUA_H
#define COMPONENT_VOXEL_SQUASH_STRETCH_LUA_H

#include <irreden/voxel/components/component_voxel_squash_stretch.hpp>
#include <irreden/script/lua_script.hpp>

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<IRComponents::C_VoxelSquashStretch> = true;

template <> inline void bindLuaType<IRComponents::C_VoxelSquashStretch>(LuaScript &luaScript) {
    luaScript.registerType<
        IRComponents::C_VoxelSquashStretch,
        IRComponents::C_VoxelSquashStretch(float, float, float, float, float, float)>(
        "C_VoxelSquashStretch",
        "stretchStrength",
        &IRComponents::C_VoxelSquashStretch::stretchStrength_,
        "squashStrength",
        &IRComponents::C_VoxelSquashStretch::squashStrength_,
        "stretchSpeedRef",
        &IRComponents::C_VoxelSquashStretch::stretchSpeedRef_,
        "squashAccelRef",
        &IRComponents::C_VoxelSquashStretch::squashAccelRef_,
        "volumePreserve",
        &IRComponents::C_VoxelSquashStretch::volumePreserve_,
        "roundness",
        &IRComponents::C_VoxelSquashStretch::roundness_,
        "impactBoost",
        &IRComponents::C_VoxelSquashStretch::impactBoost_,
        "impactSquashZ",
        &IRComponents::C_VoxelSquashStretch::impactSquashZ_,
        "impactExpandXY",
        &IRComponents::C_VoxelSquashStretch::impactExpandXY_,
        "impactDurationSec",
        &IRComponents::C_VoxelSquashStretch::impactDurationSec_,
        "springBias",
        &IRComponents::C_VoxelSquashStretch::springBias_,
        "useSpringBias",
        &IRComponents::C_VoxelSquashStretch::useSpringBias_,
        "smoothing",
        &IRComponents::C_VoxelSquashStretch::smoothing_
    );
}
} // namespace IRScript

#endif /* COMPONENT_VOXEL_SQUASH_STRETCH_LUA_H */
