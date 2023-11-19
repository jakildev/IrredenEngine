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
        UPDATE_POSITIONS_GLOBAL,
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
        RENDERING_TRIANGLES_TO_CANVAS,
        RENDERING_SINGLE_VOXEL_TO_CANVAS,
        RENDERING_CANVAS_TO_FRAMEBUFFER,
        RENDERING_UNBOUND_VOXELS_TO_TRIANGLES,
        RENDERING_UNBOUND_TRIANGLES_TO_SCREEN,
        RENDERING_FRAMEBUFFER_TO_SCREEN,
        RENDERING_TEXTURE_SCROLL,
        NUM_SYSTEMS
    };

    enum IRSystemType {
        SYSTEM_TYPE_UPDATE,
        SYSTEM_TYPE_RENDER,
        SYSTEM_TYPE_INPUT,
        SYSTEM_TYPE_OUTPUT,
        NUM_SYSTEM_TYPES
    };

    struct SystemUser {
        Archetype archetype_;
        std::function<void(ArchetypeNode*)> function_;
        Relation relation_;
    };

    template <SystemName system>
    class System;


} // namespace IRECS

#endif /* IR_SYSTEM_TYPES_H */
