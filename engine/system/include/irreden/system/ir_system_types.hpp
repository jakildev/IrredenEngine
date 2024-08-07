/*
 * Project: Irreden Engine
 * File: ir_system_types.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_SYSTEM_TYPES_H
#define IR_SYSTEM_TYPES_H

#include <irreden/entity/ir_entity_types.hpp>

#include <cstdint>
#include <functional>

using namespace IREntity;

namespace IRSystem {

    using SystemId = EntityId;

     enum SystemTickModifiers {
        SYSTEM_MODIFIER_NONE = 0,
        SYSTEM_MODIFIER_WITH_ENTITY = 1
    };

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

        // Update systems
        SCREEN_VIEW,
        VELOCITY_3D,
        ACCELERATION_3D,
        GRAVITY_3D,
        GOTO_3D,
        PERIODIC_IDLE,
        GAME_GRID,
        PLANT_GROW,
        GLOBAL_POSITION_3D,
        UPDATE_VOXEL_SET_CHILDREN,
        VOXEL_SCENE,
        VOXEL_SET_RESHAPER,
        VOXEL_POOL,
        LIFETIME,
        VIDEO_ENCODER,
        MIDI_SEQUENCE_OUT,
        PARTICLE_SPAWNER,

        // Render systems
        RENDERING_SCREEN_VIEW,
        RENDERING_TILE_SELECTOR,
        VOXEL_TO_TRIXEL_STAGE_1,
        VOXEL_TO_TRIXEL_STAGE_2,
        TRIXEL_TO_TRIXEL,
        TRIXEL_TO_FRAMEBUFFER_FRAME_DATA,
        TRIXEL_TO_FRAMEBUFFER,
        FRAMEBUFFER_TO_SCREEN,
        RENDERING_VELOCITY_2D_ISO,
        TEXTURE_SCROLL
    };

    template <typename... RelationComponents>
    struct RelationParams {
        Relation relation_ = Relation::NONE;

        RelationParams(
            Relation relation = Relation::NONE
        )
        :   relation_(relation)
        {

        }

    };
    // // Deduction guide
    // template <typename... RelationComponents>
    // RelationParams(Relation, void (*)(const RelationComponents&...)) ->
    //     RelationParams<void(*)(const RelationComponents&...), RelationComponents...>;


    // Acceptable tick function signatures
    template <
        typename FunctionTick,
        typename... Components
    >
    concept InvocableWithComponents = std::is_invocable_v<
        FunctionTick,
        Components&...
    >;

    template <
        typename FunctionTick,
        typename... Components
    >
    concept InvocableWithEntityId = std::is_invocable_v<
        FunctionTick,
        EntityId&,
        Components&...
    >;

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


    template <
        typename FunctionTick,
        typename... Components
    >
    concept InvocableWithNodeVectors = std::is_invocable_v<
        FunctionTick,
        const Archetype&,
        std::vector<EntityId>&,
        std::vector<Components>&...
    >;

    template <SystemName system>
    class System;

} // namespace IRSystem

#endif /* IR_SYSTEM_TYPES_H */
