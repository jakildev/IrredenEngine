#ifndef MIDI_POLYRHYTHM_LUA_COMPONENT_PACK_H
#define MIDI_POLYRHYTHM_LUA_COMPONENT_PACK_H

#include <irreden/common/components/component_position_3d_lua.hpp>
#include <irreden/update/components/component_velocity_3d_lua.hpp>
#include <irreden/voxel/components/component_voxel_set_lua.hpp>
#include <irreden/update/components/component_periodic_idle_lua.hpp>
#include <irreden/audio/components/component_midi_note_lua.hpp>
#include <irreden/audio/components/component_midi_sequence_lua.hpp>
#include <irreden/update/components/component_particle_burst_lua.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb_lua.hpp>
#include <irreden/update/components/component_collision_layer_lua.hpp>
#include <irreden/update/components/component_contact_event_lua.hpp>
#include <irreden/update/components/component_reactive_return_3d_lua.hpp>
#include <irreden/update/components/component_trigger_glow_lua.hpp>

namespace MidiPolyrhythm {
inline void registerLuaComponentPack(IRScript::LuaScript &luaScript) {
    using namespace IRComponents;
    luaScript.registerTypesFromTraits<
        C_Position3D,
        C_Velocity3D,
        C_VoxelSetNew,
        PeriodStage,
        C_PeriodicIdle,
        C_MidiNote,
        C_MidiSequence,
        C_ParticleBurst,
        C_ColliderIso3DAABB,
        C_CollisionLayer,
        C_ContactEvent,
        C_ReactiveReturn3D,
        C_TriggerGlow>();
}
} // namespace MidiPolyrhythm

#endif /* MIDI_POLYRHYTHM_LUA_COMPONENT_PACK_H */
