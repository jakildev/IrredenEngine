/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_particle_spawner.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// This system needs some work, expecially about
// the logic that is crammed into the VoxelParticle entity

#ifndef SYSTEM_PARTICLE_SPAWNER_H
#define SYSTEM_PARTICLE_SPAWNER_H

#include "..\ecs\ir_system_base.hpp"

#include "..\components\component_particle_spawner.hpp"
#include "..\components\component_position_3d.hpp"
#include "..\components\component_position_global_3d.hpp"


#include "..\entities\entity_voxel_particle.hpp"
#include "system_update_voxel_set_children.hpp"

using namespace IRComponents;
using namespace IRMath;

namespace IRECS {

    template<>
    class IRSystem<PARTICLE_SPAWNER> : public IRSystemBase<
        PARTICLE_SPAWNER,
        C_PositionGlobal3D,
        C_ParticleSpawner
    >   {
    public:
        IRSystem()
        {
            ENG_LOG_INFO("Created system PARTICLE_SPAWNER");
        }
        void tickWithArchetype(
            Archetype type,
            std::vector<EntityId>& entities,
            const std::vector<C_PositionGlobal3D>& positions,
            std::vector<C_ParticleSpawner>& particleSpawners
        )
        {
            for(int i=0; i < entities.size(); i++) {
                if(!particleSpawners[i].active_) {
                    continue;
                }
                // continue;
                auto& particleSpawner = particleSpawners[i];
                particleSpawner.tickCount_++;
                if(particleSpawner.tickCount_ % particleSpawner.spawnRate_ == 0) {
                    for(int j=0; j < particleSpawner.spawnCount_; j++) {
                        EntityHandle newParticle = Prefab<PrefabTypes::kVoxelParticle>::create(
                            randomVec(
                                particleSpawner.spawnRangeMin_,
                                particleSpawner.spawnRangeMax_
                            ) + vec3(0.0f, 0.0f, 5.0f), // TEMP
                            particleSpawner.color_,
                            particleSpawner.spawnLifetime_
                        );
                        // TODO: add particle spawner to scene
                        global.systemManager_->get<UPDATE_VOXEL_SET_CHILDREN>()
                            ->addEntityToScene(newParticle, entities[i]);
                    }
                }
            }
        }
    private:
        // virtual void beginExecute() override {
        //     EASY_FUNCTION(IR_PROFILER_COLOR_RENDER);
        // }

        // virtual void endExecute() override {
        //     EASY_FUNCTION(IR_PROFILER_COLOR_RENDER);
        // }

    };

} // namespace IRSystem

#endif /* SYSTEM_PARTICLE_SPAWNER_H */
