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

namespace IRECS {

    using SystemId = EntityId;

    enum SystemTickModifiers {
        SYSTEM_MODIFIER_NONE = 0,
        SYSTEM_MODIFIER_WITH_ENTITY = 1
    };

    enum SystemEvent {
        BEGIN_TICK,
        TICK,
        END_TICK,
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
        TRIXEL_TO_FRAMEBUFFER,
        FRAMEBUFFER_TO_SCREEN,
        RENDERING_VELOCITY_2D_ISO,
        TEXTURE_SCROLL,
        NUM_SYSTEMS
    };

    struct CreateSystemExtraParams {
        Relation relation_ = Relation::NONE;
        bool tickWithEntity_ = false;

    };

    template <SystemName system>
    class System;

} // namespace IRECS

#endif /* IR_SYSTEM_TYPES_H */
