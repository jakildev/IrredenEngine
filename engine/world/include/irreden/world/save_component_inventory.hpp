#ifndef SAVE_COMPONENT_INVENTORY_H
#define SAVE_COMPONENT_INVENTORY_H

// The audited per-component save-policy decision table. Every engine
// component gets exactly one IR_SAVE_OPT_IN / IR_SAVE_OPT_OUT line below;
// AllEngineComponents lists all of them so the static_assert at the bottom
// fails the build if any decision is missing. Heavy include list — pull
// this header only into world-snapshot TUs and the SaveTrait test, never
// into a widely-included header (see save_trait.hpp).

#include <irreden/world/save_trait.hpp>

#include <irreden/audio/components/component_midi_channel.hpp>
#include <irreden/audio/components/component_midi_delay.hpp>
#include <irreden/audio/components/component_midi_device.hpp>
#include <irreden/audio/components/component_midi_message.hpp>
#include <irreden/audio/components/component_midi_note.hpp>
#include <irreden/audio/components/component_midi_sequence.hpp>
#include <irreden/audio/components/component_midi_source_port.hpp>
#include <irreden/common/components/component_auto_spin.hpp>
#include <irreden/common/components/component_chunk_membership.hpp>
#include <irreden/common/components/component_controllable_unit.hpp>
#include <irreden/common/components/component_cycle.hpp>
#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_persistent.hpp>
#include <irreden/common/components/component_player.hpp>
#include <irreden/common/components/component_position_2d.hpp>
#include <irreden/common/components/component_position_2d_iso.hpp>
#include <irreden/common/components/component_position_int_2d.hpp>
#include <irreden/common/components/component_position_int_3d.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_selected.hpp>
#include <irreden/common/components/component_sim_clock.hpp>
#include <irreden/common/components/component_size_int_2d.hpp>
#include <irreden/common/components/component_size_int_3d.hpp>
#include <irreden/common/components/component_size_triangles.hpp>
#include <irreden/common/components/component_stopwatch.hpp>
#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/common/components/component_timer.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/demo/components/component_example.hpp>
#include <irreden/input/components/component_cursor_position.hpp>
#include <irreden/input/components/component_glfw_gamepad_state.hpp>
#include <irreden/input/components/component_glfw_joystick.hpp>
#include <irreden/input/components/component_hitbox_2d.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/input/components/component_key_mouse_button.hpp>
#include <irreden/input/components/component_keyboard_key.hpp>
#include <irreden/input/components/component_keyboard_key_status.hpp>
#include <irreden/input/components/component_mouse_position.hpp>
#include <irreden/input/components/component_mouse_scroll.hpp>
#include <irreden/input/systems/system_entity_hover_detect.hpp>
#include <irreden/render/components/component_active_lod_level.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_canvas_target.hpp>
#include <irreden/render/components/component_color_hsva.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_detached_revoxelize_buffer.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_frame_data_trixel_to_framebuffer.hpp>
#include <irreden/render/components/component_geometric_shape.hpp>
#include <irreden/render/components/component_gizmo_handle.hpp>
#include <irreden/render/components/component_gpu_particle_pool.hpp>
#include <irreden/render/components/component_gui_hover_state.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_layout_leaf.hpp>
#include <irreden/render/components/component_layout_state.hpp>
#include <irreden/render/components/component_light_blocker.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_render_cache.hpp>
#include <irreden/render/components/component_splitter.hpp>
#include <irreden/render/components/component_sprite.hpp>
#include <irreden/render/components/component_sprite_animation.hpp>
#include <irreden/render/components/component_sprite_sheet.hpp>
#include <irreden/render/components/component_stateless_particle_emitters.hpp>
#include <irreden/render/components/component_text_segment.hpp>
#include <irreden/render/components/component_text_style.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/render/components/component_triangle_canvas_background.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_triangles_only_set.hpp>
#include <irreden/render/components/component_trixel_canvas_origin.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_viewport.hpp>
#include <irreden/render/components/component_voxel_selection.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/systems/system_perf_stats_overlay.hpp>
#include <irreden/spatial/components/component_spatial_index.hpp>
#include <irreden/system/components/component_system_event.hpp>
#include <irreden/system/components/component_system_relation.hpp>
#include <irreden/update/components/component_acceleration_3d.hpp>
#include <irreden/update/components/component_action_animation.hpp>
#include <irreden/update/components/component_anim_clip_color_track.hpp>
#include <irreden/update/components/component_anim_color_state.hpp>
#include <irreden/update/components/component_anim_motion_color_shift.hpp>
#include <irreden/update/components/component_animation_clip.hpp>
#include <irreden/update/components/component_collider_iso3d_aabb.hpp>
#include <irreden/update/components/component_collision_layer.hpp>
#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/components/component_direction.hpp>
#include <irreden/update/components/component_goto_easing_3d.hpp>
#include <irreden/update/components/component_gravity_3d.hpp>
#include <irreden/update/components/component_hitbox_circle.hpp>
#include <irreden/update/components/component_hitbox_rect.hpp>
#include <irreden/update/components/component_lerp_component.hpp>
#include <irreden/update/components/component_lifetime.hpp>
#include <irreden/update/components/component_magnitude.hpp>
#include <irreden/update/components/component_overlap_contact_batch.hpp>
#include <irreden/update/components/component_particle_burst.hpp>
#include <irreden/update/components/component_particle_spawner.hpp>
#include <irreden/update/components/component_periodic_idle.hpp>
#include <irreden/update/components/component_procedural_animation.hpp>
#include <irreden/update/components/component_reactive_return_3d.hpp>
#include <irreden/update/components/component_rhythmic_launch.hpp>
#include <irreden/update/components/component_rotation_target.hpp>
#include <irreden/update/components/component_spawn_glow.hpp>
#include <irreden/update/components/component_spring_platform.hpp>
#include <irreden/update/components/component_trigger_glow.hpp>
#include <irreden/update/components/component_velocity_2d_iso.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_velocity_drag.hpp>
#include <irreden/voxel/components/component_bind_points.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_joint_hierarchy.hpp>
#include <irreden/voxel/components/component_joint_name.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_squash_stretch.hpp>
#include <irreden/wip/components/component_alarm.hpp>

#include <tuple>

// Class A — GPU-handle / ResourceId-owning (process-local; W-10 regenerates post-load)
IR_SAVE_OPT_OUT(IRComponents::C_TrixelCanvasFramebuffer)
IR_SAVE_OPT_OUT(IRComponents::C_TriangleCanvasTextures)
IR_SAVE_OPT_OUT(IRComponents::C_CanvasAOTexture)
IR_SAVE_OPT_OUT(IRComponents::C_CanvasFogOfWar)
IR_SAVE_OPT_OUT(IRComponents::C_CanvasSunShadow)
IR_SAVE_OPT_OUT(IRComponents::C_CanvasLightVolume)
IR_SAVE_OPT_OUT(IRComponents::C_PerAxisTrixelCanvases)
IR_SAVE_OPT_OUT(IRComponents::C_GPUParticlePool)
IR_SAVE_OPT_OUT(IRComponents::C_StatelessParticleEmitters)
IR_SAVE_OPT_OUT(IRComponents::C_DetachedRevoxelizeBuffer)
IR_SAVE_OPT_OUT(IRComponents::C_SpriteSheet)
IR_SAVE_OPT_OUT(IRComponents::C_Sprite)

// Class B — derived / rebuildable
IR_SAVE_OPT_OUT(IRComponents::C_VoxelPool)
IR_SAVE_OPT_OUT(IRComponents::C_SpatialIndex)
IR_SAVE_OPT_OUT(IRComponents::C_RenderCache)
IR_SAVE_OPT_OUT(IRComponents::C_ActiveLodLevel)
IR_SAVE_OPT_OUT(IRComponents::C_ChunkVisibleThisFrame)
IR_SAVE_OPT_OUT(IRComponents::C_FrameDataTrixelToFramebuffer)
IR_SAVE_OPT_OUT(IRComponents::C_CanvasLocalRotation)
IR_SAVE_OPT_OUT(IRComponents::C_LayoutState)
IR_SAVE_OPT_OUT(IRComponents::C_LayoutLeaf)
IR_SAVE_OPT_OUT(IRComponents::C_ResolvedFields)

// Class C — transient per-frame events / device input
IR_SAVE_OPT_OUT(IRComponents::C_ContactEvent)
IR_SAVE_OPT_OUT(IRComponents::C_OverlapContactBatch)
IR_SAVE_OPT_OUT(IRComponents::C_CursorPosition)
IR_SAVE_OPT_OUT(IRComponents::C_MousePosition)
IR_SAVE_OPT_OUT(IRComponents::C_MouseScroll)
IR_SAVE_OPT_OUT(IRComponents::C_KeyboardKey)
IR_SAVE_OPT_OUT(IRComponents::C_KeyStatus)
IR_SAVE_OPT_OUT(IRComponents::C_KeyPressed)
IR_SAVE_OPT_OUT(IRComponents::C_KeyReleased)
IR_SAVE_OPT_OUT(IRComponents::C_KeyMouseButton)
IR_SAVE_OPT_OUT(IRComponents::C_GLFWGamepadState)
IR_SAVE_OPT_OUT(IRComponents::C_GLFWJoystick)
IR_SAVE_OPT_OUT(IRComponents::C_MidiMessage)
IR_SAVE_OPT_OUT(IRComponents::C_MidiMessageData)
IR_SAVE_OPT_OUT(IRComponents::C_MidiMessageStatus)
IR_SAVE_OPT_OUT(IRComponents::C_MidiIn)
IR_SAVE_OPT_OUT(IRComponents::C_MidiOut)

// Class D — engine/ECS-internal plumbing (archetype walk excludes these entities)
IR_SAVE_OPT_OUT(IRComponents::C_SystemRelation)
IR_SAVE_OPT_OUT(IRComponents::C_IsNotPure)
IR_SAVE_OPT_OUT(IRComponents::C_MarkedForDeletion)
IR_SAVE_OPT_OUT(IRComponents::C_NoGlobalModifiers)
IR_SAVE_OPT_OUT(IRComponents::C_GlobalModifiers)
IR_SAVE_OPT_OUT(IRComponents::C_LambdaModifiers)
IR_SAVE_OPT_OUT(IRComponents::C_Modifiers)

// C_LerpEntity holds a std::function member — same non-serializable shape
// as C_LambdaModifiers above, so it opts out too.
IR_SAVE_OPT_OUT(IRComponents::C_LerpEntity)

// Class E — C_VoxelSetNew: OPT-IN, flagged provisional (custom serializer is P2/W-3+; flip to
// OPT-OUT is one line if the slice can't absorb it)
IR_SAVE_OPT_IN(IRComponents::C_VoxelSetNew, 1)

// Class E — C_Skeleton: OPT-IN (authored rig topology; joint EntityIds round-trip under the
// snapshot's id-stable contract)
IR_SAVE_OPT_IN(IRComponents::C_Skeleton, 1)

// Class E — C_JointHierarchy: OPT-OUT (deprecated compile shim, superseded by C_Skeleton)
IR_SAVE_OPT_OUT(IRComponents::C_JointHierarchy)

// Class F — plain gameplay data
IR_SAVE_OPT_IN(IRComponents::C_LocalTransform, 1)
IR_SAVE_OPT_IN(IRComponents::C_WorldTransform, 1)
IR_SAVE_OPT_IN(IRComponents::C_Position2D, 1)
IR_SAVE_OPT_IN(IRComponents::C_Position2DIso, 1)
IR_SAVE_OPT_IN(IRComponents::C_PositionInt2D, 1)
IR_SAVE_OPT_IN(IRComponents::C_PositionInt3D, 1)
IR_SAVE_OPT_IN(IRComponents::C_SizeInt2D, 1)
IR_SAVE_OPT_IN(IRComponents::C_SizeInt3D, 1)
IR_SAVE_OPT_IN(IRComponents::C_SizeTriangles, 1)
IR_SAVE_OPT_IN(IRComponents::C_Direction3D, 1)
IR_SAVE_OPT_IN(IRComponents::C_Magnitude, 1)
IR_SAVE_OPT_IN(IRComponents::C_RotationMode, 1)
IR_SAVE_OPT_IN(IRComponents::C_RotationTarget, 1)
IR_SAVE_OPT_IN(IRComponents::C_AutoSpin, 1)
IR_SAVE_OPT_IN(IRComponents::C_ChunkMembership, 1)
IR_SAVE_OPT_IN(IRComponents::C_Velocity3D, 1)
IR_SAVE_OPT_IN(IRComponents::C_Velocity2DIso, 1)
IR_SAVE_OPT_IN(IRComponents::C_VelocityDrag, 1)
IR_SAVE_OPT_IN(IRComponents::C_Acceleration3D, 1)
IR_SAVE_OPT_IN(IRComponents::C_Gravity3D, 1)
IR_SAVE_OPT_IN(IRComponents::C_HasGravity, 1)
IR_SAVE_OPT_IN(IRComponents::C_GotoEasing3D, 1)
IR_SAVE_OPT_IN(IRComponents::C_ReactiveReturn3D, 1)
IR_SAVE_OPT_IN(IRComponents::C_SpringPlatform, 1)
IR_SAVE_OPT_IN(IRComponents::C_WallBounce, 1)
IR_SAVE_OPT_IN(IRComponents::C_WallDeath, 1)
IR_SAVE_OPT_IN(IRComponents::C_ColliderIso3DAABB, 1)
IR_SAVE_OPT_IN(IRComponents::C_CollisionLayer, 1)
IR_SAVE_OPT_IN(IRComponents::C_HitBox2D, 1)
IR_SAVE_OPT_IN(IRComponents::C_HitBox2DGui, 1)
IR_SAVE_OPT_IN(IRComponents::C_HitboxCircle, 1)
IR_SAVE_OPT_IN(IRComponents::C_HitboxRect, 1)
IR_SAVE_OPT_IN(IRComponents::C_ActiveHitbox, 1)
IR_SAVE_OPT_IN(IRComponents::C_Lifetime, 1)
IR_SAVE_OPT_IN(IRComponents::C_Timer, 1)
IR_SAVE_OPT_IN(IRComponents::C_Stopwatch, 1)
IR_SAVE_OPT_IN(IRComponents::C_Cycle, 1)
IR_SAVE_OPT_IN(IRComponents::C_Loop, 1)
IR_SAVE_OPT_IN(IRComponents::C_Alarm, 1)
IR_SAVE_OPT_IN(IRComponents::C_PeriodicIdle, 1)
IR_SAVE_OPT_IN(IRComponents::C_SimClock, 1)
IR_SAVE_OPT_IN(IRComponents::C_AnimationClip, 1)
IR_SAVE_OPT_IN(IRComponents::C_ActionAnimation, 1)
IR_SAVE_OPT_IN(IRComponents::C_AnimClipColorTrack, 1)
IR_SAVE_OPT_IN(IRComponents::C_AnimColorState, 1)
IR_SAVE_OPT_IN(IRComponents::C_AnimMotionColorShift, 1)
IR_SAVE_OPT_IN(IRComponents::C_ProceduralAnimation, 1)
IR_SAVE_OPT_IN(IRComponents::C_VoxelSquashStretch, 1)
IR_SAVE_OPT_IN(IRComponents::C_SpriteAnimation, 1)
IR_SAVE_OPT_IN(IRComponents::C_TextureScrollPosition, 1)
IR_SAVE_OPT_IN(IRComponents::C_TextureScrollVelocity, 1)
IR_SAVE_OPT_IN(IRComponents::C_ParticleBurst, 1)
IR_SAVE_OPT_IN(IRComponents::C_ParticleSpawner, 1)
IR_SAVE_OPT_IN(IRComponents::C_SpawnGlow, 1)
IR_SAVE_OPT_IN(IRComponents::C_TriggerGlow, 1)
IR_SAVE_OPT_IN(IRComponents::C_RhythmicLaunch, 1)
IR_SAVE_OPT_IN(IRComponents::C_ColorHSV, 1)
// C_GeometricShape is a template with no primary definition — only
// RECTANGULAR_PRISM and SPHERE specializations exist. SPHERE represents
// the family (same one-representative-instantiation approach used for
// any templated component with more than one concrete specialization).
IR_SAVE_OPT_IN(IRComponents::C_GeometricShape<IRMath::Shape3D::SPHERE>, 1)
IR_SAVE_OPT_IN(IRComponents::C_ShapeDescriptor, 1)
IR_SAVE_OPT_IN(IRComponents::C_TriangleCanvasBackground, 1)
IR_SAVE_OPT_IN(IRComponents::C_TrianglesOnlySet, 1)
IR_SAVE_OPT_IN(IRComponents::C_LightSource, 1)
IR_SAVE_OPT_IN(IRComponents::C_LightBlocker, 1)
IR_SAVE_OPT_IN(IRComponents::C_Camera, 1)
IR_SAVE_OPT_IN(IRComponents::C_CameraPosition2DIso, 1)
IR_SAVE_OPT_IN(IRComponents::C_Viewport, 1)
IR_SAVE_OPT_IN(IRComponents::C_ZoomLevel, 1)
IR_SAVE_OPT_IN(IRComponents::C_TrixelCanvasOrigin, 1)
IR_SAVE_OPT_IN(IRComponents::C_TrixelCanvasRenderBehavior, 1)
IR_SAVE_OPT_IN(IRComponents::C_EntityCanvas, 1)
IR_SAVE_OPT_IN(IRComponents::C_CanvasTarget, 1)
IR_SAVE_OPT_IN(IRComponents::C_DetachedCanvas, 1)
IR_SAVE_OPT_IN(IRComponents::C_Voxel, 1)
IR_SAVE_OPT_IN(IRComponents::C_Joint, 1)
IR_SAVE_OPT_IN(IRComponents::C_JointName, 1)
IR_SAVE_OPT_IN(IRComponents::C_BindPoints, 1)
IR_SAVE_OPT_IN(IRComponents::C_MidiSequence, 1)
IR_SAVE_OPT_IN(IRComponents::C_MidiNote, 1)
IR_SAVE_OPT_IN(IRComponents::C_MidiChannel, 1)
IR_SAVE_OPT_IN(IRComponents::C_MidiDevice, 1)
IR_SAVE_OPT_IN(IRComponents::C_MidiDelay, 1)
IR_SAVE_OPT_IN(IRComponents::C_MidiSourcePort, 1)
IR_SAVE_OPT_IN(IRComponents::C_TextSegment, 1)
IR_SAVE_OPT_IN(IRComponents::C_TextStyle, 1)
IR_SAVE_OPT_IN(IRComponents::C_GuiPosition, 1)
IR_SAVE_OPT_IN(IRComponents::C_GuiElement, 1)
IR_SAVE_OPT_IN(IRComponents::C_GuiHoverState, 1)
IR_SAVE_OPT_IN(IRComponents::C_Widget, 1)
IR_SAVE_OPT_IN(IRComponents::C_WidgetButton, 1)
IR_SAVE_OPT_IN(IRComponents::C_WidgetCheckbox, 1)
IR_SAVE_OPT_IN(IRComponents::C_WidgetColorSwatch, 1)
IR_SAVE_OPT_IN(IRComponents::C_WidgetDropdown, 1)
IR_SAVE_OPT_IN(IRComponents::C_WidgetLabel, 1)
IR_SAVE_OPT_IN(IRComponents::C_WidgetList, 1)
IR_SAVE_OPT_IN(IRComponents::C_WidgetPanel, 1)
IR_SAVE_OPT_IN(IRComponents::C_WidgetRadio, 1)
IR_SAVE_OPT_IN(IRComponents::C_WidgetScroll, 1)
IR_SAVE_OPT_IN(IRComponents::C_WidgetSlider, 1)
IR_SAVE_OPT_IN(IRComponents::C_WidgetTextInput, 1)
IR_SAVE_OPT_IN(IRComponents::C_WidgetState, 1)
IR_SAVE_OPT_IN(IRComponents::C_Splitter, 1)
IR_SAVE_OPT_IN(IRComponents::C_VoxelSelection, 1)
IR_SAVE_OPT_IN(IRComponents::C_VoxelSelectionHighlight, 1)
IR_SAVE_OPT_IN(IRComponents::C_GizmoHandle, 1)
IR_SAVE_OPT_IN(IRComponents::C_Player, 1)
IR_SAVE_OPT_IN(IRComponents::C_ControllableUnit, 1)
IR_SAVE_OPT_IN(IRComponents::C_Selected, 1)
IR_SAVE_OPT_IN(IRComponents::C_Persistent, 1)
IR_SAVE_OPT_IN(IRComponents::C_Name, 1)
IR_SAVE_OPT_IN(IRComponents::C_Example, 1)
IR_SAVE_OPT_IN(IRComponents::C_HelpText, 1)
IR_SAVE_OPT_IN(IRComponents::C_SpatialQueryable, 1)

// C_EntityHoverDetectTag and C_PerfStatsOverlayTag are defined inline in
// their owning system files (system_entity_hover_detect.hpp /
// system_perf_stats_overlay.hpp) inside namespace IRSystem, not
// IRComponents, despite the C_ naming convention.
IR_SAVE_OPT_IN(IRSystem::C_EntityHoverDetectTag, 1)
IR_SAVE_OPT_IN(IRSystem::C_PerfStatsOverlayTag, 1)

// Class D (cont.) — C_SystemEvent<TICK> represents the C_SystemEvent<…>
// family (BEGIN_TICK/TICK/END_TICK/RELATION_TICK); the archetype walk
// excludes system-registration entities entirely (Class D), so the other
// three specializations are never queried by the walker and do not need
// their own tuple entries.
IR_SAVE_OPT_OUT(IRComponents::C_SystemEvent<IRSystem::TICK>)

namespace IRWorld {

using AllEngineComponents = std::tuple<
    IRComponents::C_TrixelCanvasFramebuffer,
    IRComponents::C_TriangleCanvasTextures,
    IRComponents::C_CanvasAOTexture,
    IRComponents::C_CanvasFogOfWar,
    IRComponents::C_CanvasSunShadow,
    IRComponents::C_CanvasLightVolume,
    IRComponents::C_PerAxisTrixelCanvases,
    IRComponents::C_GPUParticlePool,
    IRComponents::C_StatelessParticleEmitters,
    IRComponents::C_DetachedRevoxelizeBuffer,
    IRComponents::C_SpriteSheet,
    IRComponents::C_Sprite,
    IRComponents::C_VoxelPool,
    IRComponents::C_SpatialIndex,
    IRComponents::C_RenderCache,
    IRComponents::C_ActiveLodLevel,
    IRComponents::C_ChunkVisibleThisFrame,
    IRComponents::C_FrameDataTrixelToFramebuffer,
    IRComponents::C_CanvasLocalRotation,
    IRComponents::C_LayoutState,
    IRComponents::C_LayoutLeaf,
    IRComponents::C_ResolvedFields,
    IRComponents::C_ContactEvent,
    IRComponents::C_OverlapContactBatch,
    IRComponents::C_CursorPosition,
    IRComponents::C_MousePosition,
    IRComponents::C_MouseScroll,
    IRComponents::C_KeyboardKey,
    IRComponents::C_KeyStatus,
    IRComponents::C_KeyPressed,
    IRComponents::C_KeyReleased,
    IRComponents::C_KeyMouseButton,
    IRComponents::C_GLFWGamepadState,
    IRComponents::C_GLFWJoystick,
    IRComponents::C_MidiMessage,
    IRComponents::C_MidiMessageData,
    IRComponents::C_MidiMessageStatus,
    IRComponents::C_MidiIn,
    IRComponents::C_MidiOut,
    IRComponents::C_SystemRelation,
    IRComponents::C_IsNotPure,
    IRComponents::C_MarkedForDeletion,
    IRComponents::C_NoGlobalModifiers,
    IRComponents::C_GlobalModifiers,
    IRComponents::C_LambdaModifiers,
    IRComponents::C_Modifiers,
    IRComponents::C_VoxelSetNew,
    IRComponents::C_Skeleton,
    IRComponents::C_JointHierarchy,
    IRComponents::C_LocalTransform,
    IRComponents::C_WorldTransform,
    IRComponents::C_Position2D,
    IRComponents::C_Position2DIso,
    IRComponents::C_PositionInt2D,
    IRComponents::C_PositionInt3D,
    IRComponents::C_SizeInt2D,
    IRComponents::C_SizeInt3D,
    IRComponents::C_SizeTriangles,
    IRComponents::C_Direction3D,
    IRComponents::C_Magnitude,
    IRComponents::C_RotationMode,
    IRComponents::C_RotationTarget,
    IRComponents::C_AutoSpin,
    IRComponents::C_ChunkMembership,
    IRComponents::C_Velocity3D,
    IRComponents::C_Velocity2DIso,
    IRComponents::C_VelocityDrag,
    IRComponents::C_Acceleration3D,
    IRComponents::C_Gravity3D,
    IRComponents::C_HasGravity,
    IRComponents::C_GotoEasing3D,
    IRComponents::C_ReactiveReturn3D,
    IRComponents::C_SpringPlatform,
    IRComponents::C_WallBounce,
    IRComponents::C_WallDeath,
    IRComponents::C_ColliderIso3DAABB,
    IRComponents::C_CollisionLayer,
    IRComponents::C_HitBox2D,
    IRComponents::C_HitBox2DGui,
    IRComponents::C_HitboxCircle,
    IRComponents::C_HitboxRect,
    IRComponents::C_ActiveHitbox,
    IRComponents::C_Lifetime,
    IRComponents::C_Timer,
    IRComponents::C_Stopwatch,
    IRComponents::C_Cycle,
    IRComponents::C_Loop,
    IRComponents::C_Alarm,
    IRComponents::C_PeriodicIdle,
    IRComponents::C_SimClock,
    IRComponents::C_AnimationClip,
    IRComponents::C_ActionAnimation,
    IRComponents::C_AnimClipColorTrack,
    IRComponents::C_AnimColorState,
    IRComponents::C_AnimMotionColorShift,
    IRComponents::C_ProceduralAnimation,
    IRComponents::C_LerpEntity,
    IRComponents::C_VoxelSquashStretch,
    IRComponents::C_SpriteAnimation,
    IRComponents::C_TextureScrollPosition,
    IRComponents::C_TextureScrollVelocity,
    IRComponents::C_ParticleBurst,
    IRComponents::C_ParticleSpawner,
    IRComponents::C_SpawnGlow,
    IRComponents::C_TriggerGlow,
    IRComponents::C_RhythmicLaunch,
    IRComponents::C_ColorHSV,
    IRComponents::C_GeometricShape<IRMath::Shape3D::SPHERE>,
    IRComponents::C_ShapeDescriptor,
    IRComponents::C_TriangleCanvasBackground,
    IRComponents::C_TrianglesOnlySet,
    IRComponents::C_LightSource,
    IRComponents::C_LightBlocker,
    IRComponents::C_Camera,
    IRComponents::C_CameraPosition2DIso,
    IRComponents::C_Viewport,
    IRComponents::C_ZoomLevel,
    IRComponents::C_TrixelCanvasOrigin,
    IRComponents::C_TrixelCanvasRenderBehavior,
    IRComponents::C_EntityCanvas,
    IRComponents::C_CanvasTarget,
    IRComponents::C_DetachedCanvas,
    IRComponents::C_Voxel,
    IRComponents::C_Joint,
    IRComponents::C_JointName,
    IRComponents::C_BindPoints,
    IRComponents::C_MidiSequence,
    IRComponents::C_MidiNote,
    IRComponents::C_MidiChannel,
    IRComponents::C_MidiDevice,
    IRComponents::C_MidiDelay,
    IRComponents::C_MidiSourcePort,
    IRComponents::C_TextSegment,
    IRComponents::C_TextStyle,
    IRComponents::C_GuiPosition,
    IRComponents::C_GuiElement,
    IRComponents::C_GuiHoverState,
    IRComponents::C_Widget,
    IRComponents::C_WidgetButton,
    IRComponents::C_WidgetCheckbox,
    IRComponents::C_WidgetColorSwatch,
    IRComponents::C_WidgetDropdown,
    IRComponents::C_WidgetLabel,
    IRComponents::C_WidgetList,
    IRComponents::C_WidgetPanel,
    IRComponents::C_WidgetRadio,
    IRComponents::C_WidgetScroll,
    IRComponents::C_WidgetSlider,
    IRComponents::C_WidgetTextInput,
    IRComponents::C_WidgetState,
    IRComponents::C_Splitter,
    IRComponents::C_VoxelSelection,
    IRComponents::C_VoxelSelectionHighlight,
    IRComponents::C_GizmoHandle,
    IRComponents::C_Player,
    IRComponents::C_ControllableUnit,
    IRComponents::C_Selected,
    IRComponents::C_Persistent,
    IRComponents::C_Name,
    IRComponents::C_Example,
    IRComponents::C_HelpText,
    IRComponents::C_SpatialQueryable,
    IRSystem::C_EntityHoverDetectTag,
    IRSystem::C_PerfStatsOverlayTag,
    IRComponents::C_SystemEvent<IRSystem::TICK>>;

inline constexpr std::size_t kExpectedEngineComponentCount = 165;

static_assert(
    detail::allExplicit<AllEngineComponents>(),
    "Every engine component must have an explicit IR_SAVE_OPT_IN/OPT_OUT decision."
);

} // namespace IRWorld

#endif /* SAVE_COMPONENT_INVENTORY_H */
