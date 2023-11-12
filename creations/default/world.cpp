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
    const ivec3 partitions = ivec3(2, 2, 2);
    const ivec3 batchSize = IRConstants::kVoxelPoolMaxAllocationSize;
    for(int x = 0; x < partitions.x; x++) {
        for(int y = 0; y < partitions.y; y++) {
            for(int z = 0; z < partitions.z; z++) {
                auto entities = IRECS::createEntityBatchWithFunctions(
                    batchSize / partitions,
                    [](ivec3 index){
                        return C_Position3D{
                            256, 256, 256
                        };
                    },
                    [batchSize, partitions](ivec3 index) {
                        Color color{
                            roundFloatToByte(static_cast<float>(index.x) / (batchSize.x / partitions.x)),
                            roundFloatToByte(static_cast<float>(index.y) / (batchSize.y / partitions.y)),
                            roundFloatToByte(static_cast<float>(index.z) / (batchSize.z / partitions.z)),
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
                    // [](ivec3 index) {
                    //     return C_Velocity3D{
                    //         vec3(0)
                    //     };
                    // },
                    [batchSize, partitions](ivec3 index) {
                        int face = (index.x + index.y + index.z) % 3;
                        if(face == 0) {
                            return C_Velocity3D{
                                vec3(
                                    index.x ,
                                    // IRMath::randomFloat(-1.25, 1.25) * 20.0f,
                                    0,
                                    0
                                )
                            };
                        }
                        if(face == 1) {
                            return C_Velocity3D{
                                vec3(
                                    0,
                                    // IRMath::randomFloat(-1.25, 1.25) * 20.0f,
                                    index.y,
                                    0
                                )
                            };
                        }
                        if(face == 2) {
                            return C_Velocity3D{
                                vec3(
                                    0,
                                    0,
                                    // IRMath::randomFloat(-1.25, 1.25) * 20.0f
                                    index.z
                                )
                            };
                        }

                        return C_Velocity3D{vec3(0, 0, 0)};
                    }
                );

                EntityId parent = IRECS::createEntity(
                    C_Position3D{
                        static_cast<float>(x * 32),
                        static_cast<float>(y * 32),
                        static_cast<float>(z * 32)
                    }
                );
                for(auto& entity : entities) {
                    IRECS::setParent(entity, parent);
                }
            }
        }
    }
}

void World::initGameSystems()
{

}