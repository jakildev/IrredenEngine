/*
 * Project: Irreden Engine
 * File: entity_voxel_particle.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

// Notes: This was sort of hacked together but perhaps
// this should just be a new single voxel with a particle
// component, and not a whole voxel set.

#ifndef ENTITY_VOXEL_PARTICLE_H
#define ENTITY_VOXEL_PARTICLE_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_acceleration_3d.hpp>
#include <irreden/update/components/component_goto_easing_3d.hpp>
#include <irreden/update/components/component_periodic_idle.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>


using namespace IRMath;
using namespace IRComponents;

namespace IRECS {

    // New system should be exactly the same as this!
    // this should also just use single createEntity call!
    template <>
    struct Prefab<PrefabTypes::kVoxelParticle> {
        static EntityId create(
            vec3 position,
            Color color,
            int lifetime = 120
        )
        {
            EntityId entity = IRECS::createEntity();
            IRECS::setComponent(entity, C_Position3D{position});
            IRECS::setComponent(entity, C_VoxelSetNew{
                ivec3(1, 1, 1),
                color
            });
            const float periodAmplitude = 64.0f;
            const float periodVariance = 8.0f;
            float gotoHeight = randomFloat(
                periodAmplitude - periodVariance,
                periodAmplitude + periodVariance
            );
            // IRECS::setComponent(entity, C_Velocity3D{vec3(0, 0, -20.0f)});
            // IRECS::setComponent(entity, C_Acceleration3D{vec3(0, 0, -3.5f)});

            auto& periodicIdle = IRECS::setComponent(entity, C_PeriodicIdle{
                gotoHeight,
                (float)lifetime / IRConstants::kFPS
            });

            periodicIdle.addStagePeriodRange(
                0.0f,
                static_cast<float>(M_PI) * 2.0f,
                0.0f,
                1.75f,
                IREasingFunctions::kQuadraticEaseOut
            );
            // periodicIdle.appendStageDurationPeriod(
            //     static_cast<float>(M_PI) / 2.0f,
            //     0.50f,
            //     1.0f,
            //     IREasingFunctions::kLinearInterpolation
            // );
            // periodicIdle.appendStageDurationPeriod(
            //     static_cast<float>(M_PI) / 2.0f,
            //     1.5f,
            //     1.75f,
            //     IREasingFunctions::kQuadraticEaseOut
            // );
            // periodicIdle.appendStageDurationPeriod(
            //     static_cast<float>(M_PI) / 2.0f,
            //     1.75f,
            //     1.875f,
            //     IREasingFunctions::kQuadraticEaseOut
            // );

            // IRECS::setComponent(entity, C_Velocity3D{startVelocity});

            // Temp, particles always float up
            // IRECS::setComponent(entity, C_Acceleration3D{vec3(0, 0, -3.5f)});
            // IRECS::setComponent(entity, C_HasGravity{});
            IRECS::setComponent(entity, C_Lifetime{lifetime});


            return entity;
        }
    };

}


#endif /* ENTITY_VOXEL_PARTICLE_H */
