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
#include <irreden/update/components/component_rhythmic_launch_lua.hpp>
#include <irreden/update/components/component_trigger_glow_lua.hpp>
#include <irreden/update/components/component_spawn_glow_lua.hpp>
#include <irreden/update/components/component_animation_clip_lua.hpp>
#include <irreden/update/components/component_action_animation_lua.hpp>
#include <irreden/update/components/component_anim_clip_color_track_lua.hpp>
#include <irreden/update/components/component_anim_color_state_lua.hpp>
#include <irreden/update/components/component_anim_motion_color_shift_lua.hpp>
#include <irreden/render/components/component_triangle_canvas_background_lua.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior_lua.hpp>
#include <irreden/render/components/component_zoom_level_lua.hpp>

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
        C_RhythmicLaunch,
        C_TriggerGlow,
        C_SpawnGlow,
        ActionAnimationPhase,
        C_AnimationClip,
        AnimationBinding,
        C_ActionAnimation,
        AnimPhaseColor,
        AnimPhaseColorMod,
        C_AnimClipColorTrack,
        C_AnimColorState,
        C_AnimMotionColorShift,
        BackgroundTypes,
        C_TriangleCanvasBackground,
        C_TrixelCanvasRenderBehavior,
        C_ZoomLevel>();
}
} // namespace MidiPolyrhythm

#endif /* MIDI_POLYRHYTHM_LUA_COMPONENT_PACK_H */
