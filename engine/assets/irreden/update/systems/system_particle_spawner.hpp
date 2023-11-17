/*
 * Project: Irreden Engine
 * File: system_particle_spawner.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// This system needs some work, expecially about
// the logic that is crammed into the VoxelParticle entity

#ifndef SYSTEM_PARTICLE_SPAWNER_H
#define SYSTEM_PARTICLE_SPAWNER_H

#include <irreden/ir_ecs.hpp>

#include <irreden/update/components/component_particle_spawner.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>

#include <irreden/voxel/entities/entity_voxel_particle.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    struct System<PARTICLE_SPAWNER> {
        static SystemId create() {
            return createSystem<C_ParticleSpawner, C_PositionGlobal3D>(
                "ParticleSpawner",
                [](
                    C_ParticleSpawner& particleSpawner,
                    C_PositionGlobal3D& position
                )
                {
                    if(!particleSpawner.active_) {
                        return;
                    }
                    particleSpawner.tickCount_++;
                    if(particleSpawner.tickCount_ % particleSpawner.spawnRate_ == 0) {
                        for(int j=0; j < particleSpawner.spawnCount_; j++) {
                            IRECS::createEntity<kVoxelParticle>(
                                randomVec(
                                    particleSpawner.spawnRangeMin_,
                                    particleSpawner.spawnRangeMax_
                                ) + vec3(0.0f, 0.0f, 5.0f), // TEMP
                                particleSpawner.color_,
                                particleSpawner.spawnLifetime_
                            );
                            // TODO: add particle spawner to scene
                            // IRECS::getEngineSystem<UPDATE_VOXEL_SET_CHILDREN>()
                            //     .addEntityToScene(newParticle, entities[i]);
                        }
                    }
                }
            );
        }
    };

} // namespace System

#endif /* SYSTEM_PARTICLE_SPAWNER_H */
