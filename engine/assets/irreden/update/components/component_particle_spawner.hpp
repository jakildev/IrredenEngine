/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_particle_spawner.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_PARTICLE_SPAWNER_H
#define COMPONENT_PARTICLE_SPAWNER_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/common/components/component_tags_all.hpp>

using namespace IRMath;

namespace IRComponents {


    // This could probably be broken up into smaller components
    struct C_ParticleSpawner {
        bool active_ = false;
        int tickCount_;
        int spawnRate_;
        int spawnCount_;
        int spawnLifetime_;
        vec3 spawnRangeMin_;
        vec3 spawnRangeMax_;
        Color color_;
        float minGotoHeight_;
        float maxGotoHeight_;


        C_ParticleSpawner(
            int spawnRate,
            int spawnCount,
            int spawnLifetime,
            Color color,
            vec3 spawnRangeMin = vec3(0, 0, 0),
            vec3 spawnRangeMax = vec3(0, 0, 0),
            float minGotoHeight = 0.0f,
            float maxGotoHeight = 0.0f,
            int maxParticles = 1000
        )
        :   tickCount_{0}
        ,   spawnRate_{spawnRate}
        ,   spawnCount_{spawnCount}
        ,   spawnLifetime_{spawnLifetime}
        ,   color_{color}
        ,   spawnRangeMin_{spawnRangeMin}
        ,   spawnRangeMax_{spawnRangeMax}
        ,   minGotoHeight_{minGotoHeight}
        ,   maxGotoHeight_{maxGotoHeight}
        {

        }

        // Default
        C_ParticleSpawner()
        :   C_ParticleSpawner(
                1,
                0,
                0,
                Color(255, 255, 255, 255)
            )
        {

        }

        void activate() {
            active_ = true;
        }

        void deactivate() {
            active_ = false;
        }

    };

} // namespace IRComponents


#endif /* COMPONENT_PARTICLE_SPAWNER_H */
