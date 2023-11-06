#include "world.hpp"

// #include <irreden/voxel/entit>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_acceleration_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/update/components/component_goto_easing_3d.hpp>

World::World(int argc, char **argv)
:   IRWorld(argc, argv)
{
    IRProfile::logInfo("Creating world: XxxxxXxxxx");
}

World::~World()
{

}

void World::initGameEntities()
{
    std::vector<C_Position3D> positions;
    std::vector<C_Velocity3D> velocities;
    std::vector<C_Acceleration3D> accelerations;
    std::vector<C_VoxelSetNew> voxelSets;
    std::vector<C_GotoEasing3D> gotoEasings;

    for(int i = 0; i < IRConstants::kVoxelPoolSize.x / 2; i++) {
        for(int j = 0; j < IRConstants::kVoxelPoolSize.y / 2; j++) {
            for(int k = 0; k < IRConstants::kVoxelPoolSize.z / 2; k++) {
                positions.emplace_back(
                    C_Position3D{
                        vec3(256, 256, 256)
                    }
                );

                voxelSets.emplace_back(
                    C_VoxelSetNew(
                        ivec3(1, 1, 1),
                        Color{
                            (uint8_t)(i * IRConstants::kVoxelPoolSize.x / 2),
                            (uint8_t)(j * IRConstants::kVoxelPoolSize.y / 2),
                            (uint8_t)(k * IRConstants::kVoxelPoolSize.z / 2),
                            255,
                        }
                    )
                );

                gotoEasings.emplace_back(
                    C_GotoEasing3D{
                        C_Position3D{
                            vec3(256, 256, 256)
                        },
                        C_Position3D{
                            vec3(i * 1, j * 1, k * 1) + vec3(256)
                        },
                        (-i + -j + -k + 400) / 100.0f,
                        IREasingFunctions::kBounceEaseOut
                    }
                );
                int face = (i + j + k) % 3;
                if(face == 0) {
                    velocities.emplace_back(
                        C_Velocity3D{
                            vec3(i, 0, 0)
                        }
                    );
                    accelerations.emplace_back(
                        C_Acceleration3D{
                            vec3(
                                IRMath::randomFloat(-0.5, 0.5),
                                0,
                                0
                            )
                        }
                    );
                }
                if(face == 1) {
                    velocities.emplace_back(
                        C_Velocity3D{
                            vec3(0, j, 0)
                        }
                    );
                    accelerations.emplace_back(
                        C_Acceleration3D{
                            vec3(
                                0,
                                IRMath::randomFloat(-0.5, 0.5),
                                0
                            )
                        }
                    );
                }
                if(face == 2) {
                    velocities.emplace_back(
                        C_Velocity3D{
                            vec3(0, 0, k)
                        }
                    );
                    accelerations.emplace_back(
                        C_Acceleration3D{
                            vec3(
                                0,
                                0,
                                IRMath::randomFloat(-0.5, 0.5)
                            )
                        }
                    );
                }
                // if ((i + j + k) % 4 == 0) {
                //     world->bindEntityToCommand<IRCommands::STOP_VELOCITY>(voxelGroupTestBlock1);
                // }
            }
        }
    }
    auto entities = createEntitiesBatch(
        positions,
        voxelSets,
        gotoEasings,
        velocities,
        accelerations
    );
    for(auto& entity : entities) {
        addEntityToScene(entity);
    }
}

void World::initGameSystems()
{

}