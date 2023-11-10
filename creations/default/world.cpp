#include "world.hpp"

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
    ivec3 batchSize = ivec3(32, 32, 32);
    for(int x = 0; x < 2; x++) {
        for(int y = 0; y < 2; y++) {
            for(int z = 0; z < 2; z++) {
                auto entities = IRECS::createEntitiesBatchWithFunctions(
                    batchSize,
                    [](ivec3 index){
                        return C_Position3D{
                            256, 256, 256
                        };
                    },
                    [batchSize](ivec3 index) {
                        Color color{
                            roundFloatToByte(static_cast<float>(index.x) / batchSize.x),
                            roundFloatToByte(static_cast<float>(index.y) / batchSize.y),
                            roundFloatToByte(static_cast<float>(index.z) / batchSize.z),
                            255
                        };
                        return C_VoxelSetNew(
                            ivec3(1, 1, 1),
                            color
                        );
                    },
                    [](ivec3 index) {
                        return C_GotoEasing3D{
                            C_Position3D{
                                vec3(256, 256, 256)
                            },
                            C_Position3D{
                                vec3(index.x, index.y, index.z) + vec3(256)
                            },
                            (-index.x + -index.y + -index.z + 400) / 100.0f,
                            IREasingFunctions::kBounceEaseOut
                        };
                    },
                    [](ivec3 index) {
                        return C_Velocity3D{
                            vec3(0)
                        };
                    },
                    [](ivec3 index) {
                        int face = (index.x + index.y + index.z) % 3;
                        if(face == 0) {
                            return C_Acceleration3D{
                                vec3(
                                    IRMath::randomFloat(-1.25, 1.25),
                                    0,
                                    0
                                )
                            };
                        }
                        if(face == 1) {
                            return C_Acceleration3D{
                                vec3(
                                    0,
                                    IRMath::randomFloat(-1.25, 1.25),
                                    0
                                )
                            };
                        }
                        if(face == 2) {
                            return C_Acceleration3D{
                                vec3(
                                    0,
                                    0,
                                    IRMath::randomFloat(-1.25, 1.25)
                                )
                            };
                        }

                        return C_Acceleration3D{vec3(0, 0, 0)};
                    }
                );
                EntityId parent = IRECS::createEntity(
                    C_Position3D{
                        static_cast<float>(x * 48),
                        static_cast<float>(y * 48),
                        static_cast<float>(z * 48)
                    }
                    // ,C_Velocity3D{vec3(x * 4, y * 4, z * 4)}
                );
                for(auto& entity : entities) {
                    IRECS::setParent(entity, parent);
                }
            }
        }
    }

}


// void World::initGameEntities()
// {
//     std::vector<C_Position3D> positions;
//     std::vector<C_Velocity3D> velocities;
//     std::vector<C_Acceleration3D> accelerations;
//     std::vector<C_VoxelSetNew> voxelSets;
//     std::vector<C_GotoEasing3D> gotoEasings;

//     for(int i = 0; i < IRConstants::kVoxelPoolSize.x / 2; i++) {
//         for(int j = 0; j < IRConstants::kVoxelPoolSize.y / 2; j++) {
//             for(int k = 0; k < IRConstants::kVoxelPoolSize.z / 2; k++) {
//                 positions.emplace_back(
//                     C_Position3D{
//                         vec3(256, 256, 256)
//                     }
//                 );

//                 voxelSets.emplace_back(
//                     C_VoxelSetNew(
//                         ivec3(1, 1, 1),
//                         Color{
//                             (uint8_t)(i * IRConstants::kVoxelPoolSize.x / 2),
//                             (uint8_t)(j * IRConstants::kVoxelPoolSize.y / 2),
//                             (uint8_t)(k * IRConstants::kVoxelPoolSize.z / 2),
//                             255,
//                         }
//                     )
//                 );

//                 gotoEasings.emplace_back(
//                     C_GotoEasing3D{
//                         C_Position3D{
//                             vec3(256, 256, 256)
//                         },
//                         C_Position3D{
//                             vec3(i * 1, j * 1, k * 1) + vec3(256)
//                         },
//                         (-i + -j + -k + 400) / 100.0f,
//                         IREasingFunctions::kBounceEaseOut
//                     }
//                 );
//                 int face = (i + j + k) % 3;
//                 if(face == 0) {
//                     velocities.emplace_back(
//                         C_Velocity3D{
//                             vec3(i, 0, 0)
//                         }
//                     );
//                     accelerations.emplace_back(
//                         C_Acceleration3D{
//                             vec3(
//                                 IRMath::randomFloat(-0.5, 0.5),
//                                 0,
//                                 0
//                             )
//                         }
//                     );
//                 }
//                 if(face == 1) {
//                     velocities.emplace_back(
//                         C_Velocity3D{
//                             vec3(0, j, 0)
//                         }
//                     );
//                     accelerations.emplace_back(
//                         C_Acceleration3D{
//                             vec3(
//                                 0,
//                                 IRMath::randomFloat(-0.5, 0.5),
//                                 0
//                             )
//                         }
//                     );
//                 }
//                 if(face == 2) {
//                     velocities.emplace_back(
//                         C_Velocity3D{
//                             vec3(0, 0, k)
//                         }
//                     );
//                     accelerations.emplace_back(
//                         C_Acceleration3D{
//                             vec3(
//                                 0,
//                                 0,
//                                 IRMath::randomFloat(-0.5, 0.5)
//                             )
//                         }
//                     );
//                 }
//                 // if ((i + j + k) % 4 == 0) {
//                 //     world->bindEntityToCommand<IRCommands::STOP_VELOCITY>(voxelGroupTestBlock1);
//                 // }
//             }
//         }
//     }
//     auto entities = createEntitiesBatch(
//         positions,
//         voxelSets,
//         gotoEasings,
//         velocities,
//         accelerations
//     );
//     // for(auto& entity : entities) {
//     //     addEntityToScene(entity);
//     // }
// }

void World::initGameSystems()
{

}