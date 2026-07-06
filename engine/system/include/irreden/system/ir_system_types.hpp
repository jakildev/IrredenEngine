#ifndef IR_SYSTEM_TYPES_H
#define IR_SYSTEM_TYPES_H

#include <irreden/entity/ir_entity_types.hpp>

using namespace IREntity;

namespace IRSystem {

using SystemId = EntityId;

enum SystemTickModifiers { SYSTEM_MODIFIER_NONE = 0, SYSTEM_MODIFIER_WITH_ENTITY = 1 };

/// Per-system dispatch policy. T-222 Phase 2 of the multithreading epic
/// (#226). Default `SERIAL` keeps every existing system unchanged.
///
/// - `SERIAL`        — run the tick on the main thread, one archetype
///                     node at a time (legacy behavior).
/// - `PARALLEL_FOR`  — within each matched archetype node, split the
///                     entity range into chunks of `grainSize` and
///                     dispatch each chunk to a worker via
///                     `IRJob::parallelFor`. `beginTick`/`endTick` and
///                     archetype iteration order still serialize on the
///                     main thread.
/// - `MAIN_THREAD`   — like `SERIAL`, but documents that the system
///                     MUST run on the main thread (Lua-bound side
///                     effects, GPU calls, etc.). Set by the validator
///                     when a tick body uses a not-safe surface.
enum class Concurrency {
    SERIAL = 0,
    PARALLEL_FOR,
    MAIN_THREAD,
};

/// Default chunk size for `Concurrency::PARALLEL_FOR`. Workload-dependent
/// in the general case; documented as a constant for now per #1069. A
/// per-system override is accepted via the trailing `int grainSize`
/// parameter to `createSystem` or the optional `static constexpr int
/// kGrainSize` member on a `System<N>` specialization.
inline constexpr int kDefaultGrainSize = 512;

enum SystemEvent {
    BEGIN_TICK,
    TICK,
    END_TICK,
    RELATION_TICK,
    START,
    STOP,
};

enum SystemName {
    NULL_SYSTEM,
    EXAMPLE,

    // Input systems
    INPUT_KEY_MOUSE,
    INPUT_MIDI_MESSAGE_IN,
    OUTPUT_MIDI_MESSAGE_OUT,
    INPUT_GAMEPAD,
    ENTITY_HOVER_DETECT,
    HITBOX_MOUSE_TEST,
    HITBOX_MOUSE_TEST_GUI,

    // Update systems
    SCREEN_VIEW,
    VELOCITY_3D,
    ACCELERATION_3D,
    GRAVITY_3D,
    GOTO_3D,
    PERIODIC_IDLE,
    PERIODIC_IDLE_POSITION_OFFSET,
    PERIODIC_IDLE_MIDI_TRIGGER,
    PERIODIC_IDLE_NOTE_BURST,
    COLLISION_EVENT_CLEAR,
    COLLISION_NOTE_PLATFORM,
    DISPATCH_LUA_OVERLAP,
    REACTIVE_RETURN_3D,
    CONTACT_MIDI_TRIGGER,
    CONTACT_NOTE_BURST,
    CONTACT_TRIGGER_GLOW,
    VELOCITY_DRAG,
    MIDI_DELAY_PROCESS,
    PLANT_GROW,
    PROPAGATE_TRANSFORM,
    AUTO_SPIN_LOCAL_TRANSFORM,
    ROTATION_TARGET_LOCAL_TRANSFORM,
    // Sim-clock substrate (engine/prefabs/irreden/common/sim_clock.hpp).
    // Order within UPDATE: SIM_CLOCK_ADVANCE first (advances the C_SimClock
    // singleton), then CYCLE_BOUNDARY_DETECT / TIMER_FIRE read the new tick
    // and raise their embedded boundary/fired events; place consumers after.
    // Stopwatches have no system (elapsed is computed on read by IRSim::).
    SIM_CLOCK_ADVANCE,
    CYCLE_BOUNDARY_DETECT,
    TIMER_FIRE,
    PROPAGATE_CHUNK_MEMBERSHIP,
    VOXEL_SQUASH_STRETCH,
    UPDATE_VOXEL_SET_CHILDREN,
    REBUILD_GRID_VOXELS,
    REBUILD_DETACHED_VOXELS,
    SEED_STAGED_VOXELS,
    PROPAGATE_CANVAS_ROTATION,
    VOXEL_SET_RESHAPER,
    VOXEL_POOL,
    LIFETIME,
    VIDEO_ENCODER,
    MIDI_SEQUENCE_OUT,
    PARTICLE_SPAWNER,
    RHYTHMIC_LAUNCH,
    SPAWN_GLOW,
    ACTION_ANIMATION,
    ANIMATION_COLOR,
    ANIMATION_MOTION_COLOR_SHIFT,
    SPRING_PLATFORM,
    SPRING_COLOR,
    SPRITE_ANIMATION_ADVANCE,
    GIZMO_SCREEN_SPACE_SIZE,
    LOD_UPDATE,
    // World-space neighbour/spatial-query index. Rebuilds the
    // C_SpatialIndex singleton each frame from C_WorldTransform +
    // C_SpatialQueryable entities; a creation places it AFTER
    // PROPAGATE_TRANSFORM (so translations are current) and BEFORE any
    // consumer that calls IRPrefab::Spatial::queryRadius. See
    // engine/prefabs/irreden/spatial/.
    BUILD_SPATIAL_INDEX,

    // Modifier framework — runs at end of UPDATE, before RENDER reads
    // C_ResolvedFields. Order: decay all three vectors (per-entity,
    // global, lambda), then resolve (global / exempt partitioned by
    // C_NoGlobalModifiers via the Exclude<> archetype filter), then
    // lambda escape hatch.
    MODIFIER_DECAY,
    GLOBAL_MODIFIER_DECAY,
    LAMBDA_MODIFIER_DECAY,
    MODIFIER_RESOLVE_GLOBAL,
    MODIFIER_RESOLVE_EXEMPT,
    MODIFIER_RESOLVE_LAMBDA,

    // Render systems
    RENDERING_SCREEN_VIEW,
    RENDERING_TILE_SELECTOR,
    RESOLVE_SUN_DIRECTION,
    // Complete voxel→trixel rasterization for every voxel-pool canvas:
    // compact, stage-1, and stage-2 dispatches run in one per-canvas tick.
    // (The former separate VOXEL_TO_TRIXEL_STAGE_2 system clobbered the
    // shared voxel SSBOs across multi-canvas scenes — see system_voxel_to_trixel.hpp.)
    VOXEL_TO_TRIXEL_STAGE_1,
    TRIXEL_TO_TRIXEL,
    TRIXEL_TO_FRAMEBUFFER_FRAME_DATA,
    TRIXEL_TO_FRAMEBUFFER,
    GUI_TEXT_RENDER,
    TEXT_TO_TRIXEL,
    FRAMEBUFFER_TO_SCREEN,
    SPRITE_TO_SCREEN,
    DEBUG_OVERLAY,
    RENDERING_VELOCITY_2D_ISO,
    TEXTURE_SCROLL,
    // Per-frame skeletal joint skin-matrix upload (#605 Phase 2.2 / #1603).
    // Writes each C_Skeleton joint's skinMatrix into the binding-18
    // EntityTransformBuffer (a contiguous block per skeleton, from the shared
    // #1396 budget). MUST run after PROPAGATE_TRANSFORM (joint world transforms
    // current) and BEFORE UPDATE_VOXEL_POSITIONS_GPU (so binding 18 is filled
    // when the prepass skins per-voxel bone slots in #605 Phase 2.3).
    UPDATE_JOINT_MATRICES,
    UPDATE_VOXEL_POSITIONS_GPU,
    UPDATE_GPU_PARTICLES,
    RENDER_GPU_PARTICLES_TO_TRIXEL,
    RENDER_STATELESS_PARTICLES_TO_TRIXEL,
    SHAPES_TO_TRIXEL,
    BUILD_LIGHT_OCCLUSION_GRID,
    COMPUTE_VOXEL_AO,
    // Hi-Z (max-depth) distance mip-chain build for voxel occlusion culling
    // (#1294 child 1/3). Runs after the geometry + AO passes (distances final)
    // and produces C_TriangleCanvasTextures::hiZMips_ for next frame's
    // chunk-occlusion pre-pass; produces only — no consumer this PR.
    COMPUTE_DISTANCE_HIZ,
    RESOLVE_PER_AXIS_SCREEN_DEPTH,
    BAKE_SUN_SHADOW_MAP,
    COMPUTE_SUN_SHADOW,
    COMPUTE_LIGHT_VOLUME,
    LIGHTING_TO_TRIXEL,
    FOG_TO_TRIXEL,
    CAMERA_MOUSE_PAN,
    CAMERA_MOUSE_ROTATE,
    CAMERA_KEY_DRAG_PAN,
    CAMERA_KEY_DRAG_ROTATE,
    CAMERA_SCROLL_ZOOM,
    AUTO_YAW_ROTATE,
    VOXEL_PICKING,
    DEBUG_CULLING_MINIMAP,
    PERF_STATS_OVERLAY,
    ENTITY_CANVAS_TO_FRAMEBUFFER,
    WIDGET_INPUT,
    WIDGET_LUA_DISPATCH,
    WIDGET_APPLY_SLIDER,
    WIDGET_APPLY_CHECKBOX,
    WIDGET_APPLY_LIST,
    WIDGET_APPLY_DROPDOWN,
    WIDGET_APPLY_RADIO,
    WIDGET_APPLY_TEXT_INPUT,
    WIDGET_APPLY_SCROLL,
    WIDGET_RENDER_PANEL,
    WIDGET_RENDER_LABEL,
    WIDGET_RENDER_BUTTON,
    WIDGET_RENDER_SLIDER,
    WIDGET_RENDER_CHECKBOX,
    WIDGET_RENDER_LIST,
    WIDGET_RENDER_DROPDOWN,
    WIDGET_RENDER_RADIO,
    WIDGET_RENDER_TEXT_INPUT,
    WIDGET_RENDER_SCROLL,
    WIDGET_RENDER_COLOR_SWATCH,

    // Layout system (F-0.2)
    LAYOUT_COMPUTE,
    WIDGET_INPUT_SPLITTER,
    WIDGET_RENDER_SPLITTER,
    WIDGET_INPUT_PANEL_DRAG,
    WIDGET_RENDER_DOCK_PREVIEW,

    // Editor gizmo interaction (INPUT pipeline; F-0.5 Phase 3).
    // GIZMO_HOVER reads the entity-id GPU readback and toggles
    // C_GizmoHandle::hover_; GIZMO_DRAG drives the drag state machine
    // (press → axis-projected translate / shift-snap rotate / scale).
    GIZMO_HOVER,
    GIZMO_DRAG,

    // Reserved for tests of the registerSystem<> member-on-System<N>
    // helper. Do not use from a creation or prefab system.
    TEST_REGISTER_SYSTEM_A,
    TEST_REGISTER_SYSTEM_B
};

template <typename... RelationComponents> struct RelationParams {
    Relation relation_ = Relation::NONE;

    RelationParams(Relation relation = Relation::NONE)
        : relation_(relation) {}
};

/// Mark archetype components that a system should EXCLUDE from its match,
/// not include. Use as a template parameter to `createSystem<...>`:
///
///     createSystem<
///         C_Modifiers,
///         C_ResolvedFields,
///         Exclude<C_NoGlobalModifiers>
///     >("ModifierResolveGlobal", [](C_Modifiers&, C_ResolvedFields&) { ... });
///
/// The wrapper partitions the pack into includes / excludes at compile
/// time, so dispatch only iterates the include components and the
/// archetype matcher rejects nodes whose type intersects the exclude set
/// (no per-entity branching).
template <typename... Tags> struct Exclude {};

namespace detail {

template <typename...> struct TypeList {};

template <typename T> struct ExtractFromExclude {
    using Included = TypeList<T>;
    using Excluded = TypeList<>;
};
template <typename... Tags> struct ExtractFromExclude<Exclude<Tags...>> {
    using Included = TypeList<>;
    using Excluded = TypeList<Tags...>;
};

template <typename A, typename B> struct ConcatTypeList;
template <typename... A, typename... B> struct ConcatTypeList<TypeList<A...>, TypeList<B...>> {
    using Type = TypeList<A..., B...>;
};

template <typename Acc, typename... Pack> struct PartitionImpl;

template <typename Inc, typename Exc> struct PartitionImpl<TypeList<Inc, Exc>> {
    using Included = Inc;
    using Excluded = Exc;
};

template <typename Inc, typename Exc, typename Head, typename... Tail>
struct PartitionImpl<TypeList<Inc, Exc>, Head, Tail...> {
    using Extract = ExtractFromExclude<Head>;
    using NextInc = typename ConcatTypeList<Inc, typename Extract::Included>::Type;
    using NextExc = typename ConcatTypeList<Exc, typename Extract::Excluded>::Type;
    using Rec = PartitionImpl<TypeList<NextInc, NextExc>, Tail...>;
    using Included = typename Rec::Included;
    using Excluded = typename Rec::Excluded;
};

template <typename... Pack>
using PartitionExcludes = PartitionImpl<TypeList<TypeList<>, TypeList<>>, Pack...>;

} // namespace detail
// // Deduction guide
// template <typename... RelationComponents>
// RelationParams(Relation, void (*)(const RelationComponents&...)) ->
//     RelationParams<void(*)(const RelationComponents&...), RelationComponents...>;

// Acceptable tick function signatures
template <typename FunctionTick, typename... Components>
concept InvocableWithComponents = std::is_invocable_v<FunctionTick, Components &...>;

template <typename FunctionTick, typename... Components>
concept InvocableWithEntityId = std::is_invocable_v<FunctionTick, EntityId &, Components &...>;

// TODO: This doesn't work due to ambiguous parameter packs
// Probably need some sort of type extractor from RelationParams.
// Deferring work for now.
// template <
//     typename FunctionTick,
//     typename... Components,
//     typename... RelationComponents
// >
// concept InvocableWithOptionalRelations = std::is_invocable_v<
//     FunctionTick,
//     Components&...,
//     std::optional<RelationComponents*>...
// >;

template <typename FunctionTick, typename... Components>
concept InvocableWithNodeVectors = std::is_invocable_v<
    FunctionTick,
    const Archetype &,
    std::vector<EntityId> &,
    std::vector<Components> &...>;

template <SystemName system> class System;

} // namespace IRSystem

#endif /* IR_SYSTEM_TYPES_H */
