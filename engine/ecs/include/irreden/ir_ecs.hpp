/*
 * Project: Irreden Engine
 * File: ir_ecs.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Should this be the only thing implementers need to include?

#ifndef IR_ECS_H
#define IR_ECS_H

#include <cstdint>
#include <set>
#include <string>

#include <irreden/ecs/prefabs.hpp>

namespace IRECS {
    class EntityManager;
    class SystemManager;

    using EntityId = std::uint64_t;
    using ComponentId = EntityId;
    using Archetype = std::set<ComponentId>;

    constexpr EntityId IR_MAX_ENTITIES =                        0x0000000001FFFFFF;
    constexpr EntityId IR_RESERVED_ENTITIES =                   0x00000000000000FF;
    constexpr EntityId IR_ENTITY_ID_BITS =                      0x00000000FFFFFFFF;
    constexpr EntityId IR_PURE_ENTITY_BIT =                     0x0000000100000000;
    constexpr EntityId IR_ENTITY_FLAG_MARKED_FOR_DELETION =     0x8000000000000000;
    constexpr EntityId kNullEntityId = 0;


    enum IRRelationType {
        CHILD_OF,
        PARENT_TO,
        SIBLING_OF
    };

    enum class IREvents {
        START,
        END
    };

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


    // Get top level systems. Most of this functionality
    // should be wrapped in other API commands here, but
    // I can see why one might want to access the systems.
    extern EntityManager* g_entityManager;
    EntityManager& getEntityManager();
    extern SystemManager* g_systemManager;
    SystemManager& getSystemManager();

    // TODO: Move this
    std::string makeComponentString(Archetype type);

    template <IRSystemName systemName>
    IRSystem<systemName>& getSystem();

    template <
        PrefabTypes type,
        typename... Args
    >
    EntityId createPrefab(Args&&... args) {
        return Prefab<type>::create(
            args...
        );
    }

    // EntityHandle createEntity();

}

#endif /* IR_ECS_H */
