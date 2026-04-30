#ifndef IR_SYSTEM_TYPES_H
#define IR_SYSTEM_TYPES_H

#include <irreden/entity/ir_entity_types.hpp>

using namespace IREntity;

namespace IRSystem {

using SystemId = EntityId;

enum SystemTickModifiers { SYSTEM_MODIFIER_NONE = 0, SYSTEM_MODIFIER_WITH_ENTITY = 1 };

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
    REACTIVE_RETURN_3D,
    CONTACT_MIDI_TRIGGER,
    CONTACT_NOTE_BURST,
    CONTACT_TRIGGER_GLOW,
    VELOCITY_DRAG,
    MIDI_DELAY_PROCESS,
    PLANT_GROW,
    GLOBAL_POSITION_3D,
    APPLY_POSITION_OFFSET,
    VOXEL_SQUASH_STRETCH,
    UPDATE_VOXEL_SET_CHILDREN,
    VOXEL_SCENE,
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

    // Modifier framework — runs at end of UPDATE, before RENDER reads
    // C_ResolvedFields. Order: decay both vectors, then resolve
    // (global / exempt partitioned by C_NoGlobalModifiers via the
    // Exclude<> archetype filter), then lambda escape hatch.
    MODIFIER_DECAY,
    GLOBAL_MODIFIER_DECAY,
    MODIFIER_RESOLVE_GLOBAL,
    MODIFIER_RESOLVE_EXEMPT,
    MODIFIER_RESOLVE_LAMBDA,

    // Render systems
    RENDERING_SCREEN_VIEW,
    RENDERING_TILE_SELECTOR,
    VOXEL_TO_TRIXEL_STAGE_1,
    VOXEL_TO_TRIXEL_STAGE_2,
    TRIXEL_TO_TRIXEL,
    TRIXEL_TO_FRAMEBUFFER_FRAME_DATA,
    TRIXEL_TO_FRAMEBUFFER,
    GUI_TEXT_RENDER,
    TEXT_TO_TRIXEL,
    FRAMEBUFFER_TO_SCREEN,
    DEBUG_OVERLAY,
    RENDERING_VELOCITY_2D_ISO,
    TEXTURE_SCROLL,
    UPDATE_VOXEL_POSITIONS_GPU,
    SHAPES_TO_TRIXEL,
    BUILD_OCCUPANCY_GRID,
    COMPUTE_VOXEL_AO,
    COMPUTE_SUN_SHADOW,
    COMPUTE_LIGHT_VOLUME,
    LIGHTING_TO_TRIXEL,
    FOG_TO_TRIXEL,
    CAMERA_MOUSE_PAN,
    DEBUG_CULLING_MINIMAP,
    PERF_STATS_OVERLAY,
    ENTITY_CANVAS_TO_FRAMEBUFFER
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
template <typename... A, typename... B>
struct ConcatTypeList<TypeList<A...>, TypeList<B...>> {
    using Type = TypeList<A..., B...>;
};

template <typename Acc, typename... Pack> struct PartitionImpl;

template <typename Inc, typename Exc>
struct PartitionImpl<TypeList<Inc, Exc>> {
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
