#ifndef CREATIONS_DEFAULT_LUA_COMPONENT_PACK_H
#define CREATIONS_DEFAULT_LUA_COMPONENT_PACK_H

#include <irreden/common/components/component_position_3d_lua.hpp>
#include <irreden/update/components/component_velocity_3d_lua.hpp>
#include <irreden/voxel/components/component_voxel_set_lua.hpp>
#include <irreden/update/components/component_periodic_idle_lua.hpp>
#include <irreden/audio/components/component_midi_note_lua.hpp>
#include <irreden/audio/components/component_midi_sequence_lua.hpp>

namespace IRDefaultCreation {
inline void registerLuaComponentPack(IRScript::LuaScript &luaScript) {
    using namespace IRComponents;
    luaScript.registerTypesFromTraits<C_Position3D, C_Velocity3D, C_VoxelSetNew, PeriodStage,
                                      C_PeriodicIdle, C_MidiNote, C_MidiSequence>();
}
} // namespace IRDefaultCreation

#endif /* CREATIONS_DEFAULT_LUA_COMPONENT_PACK_H */
