/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\entity\ir_system.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_SYSTEM_H
#define IR_SYSTEM_H

// Ideas:
// - Different systems will sort the ecs each frame. It should
//   be efficient in that it is fast if the list is already sorted.

namespace IRECS {
    enum IRSystemName {
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
        NUM_SYSTEM_TYPES
    };


    template <IRSystemName System>
    class IRSystem;

} // namespace IRECS

#endif /* IR_SYSTEM_H */