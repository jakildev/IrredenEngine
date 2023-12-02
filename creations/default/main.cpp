// #include <irreden/ir_world.hpp>
#include <irreden/ir_engine.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_acceleration_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/update/components/component_goto_easing_3d.hpp>

void initGameEntities();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: default");

    IREngine::initalize(argc, argv);
    initGameEntities();
    IREngine::gameLoop();

    return 0;
}

void initGameEntities()
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
                            0, 0, 0
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
                            // Color{255, 0, 0, 255}
                            color
                        );
                    },
                    [](ivec3 index) {
                        return C_GotoEasing3D{
                            C_Position3D{
                                vec3(0, 0, 0)
                            },
                            C_Position3D{
                                vec3(index.x, index.y, index.z)
                            },
                            (-index.x + -index.y + -index.z + 700) / 100.0f,
                            IREasingFunctions::kBounceEaseOut
                        };
                    }
                ,   [batchSize, partitions](ivec3 index) {
                        int face = (index.x + index.y + index.z) % 3;
                        if(face == 0) {
                            return C_Velocity3D{
                                vec3(
                                    // sumVecComponents(index),
                                    IRMath::randomFloat(-80, 80),
                                    0,
                                    0
                                )
                            };
                        }
                        if(face == 1) {
                            return C_Velocity3D{
                                vec3(
                                    0,
                                    IRMath::randomFloat(-80, 80),
                                    // sumVecComponents(index),
                                    0
                                )
                            };
                        }
                        if(face == 2) {
                            return C_Velocity3D{
                                vec3(
                                    0,
                                    0,
                                    // sumVecComponents(index)
                                    IRMath::randomFloat(-80, 80)
                                    // index.z
                                )
                            };
                        }

                        return C_Velocity3D{vec3(0, 0, 0)};
                    }
                );

                // for(auto &entity : entities) {
                //     if(randomInt(0, 50) > 0)
                //         IRECS::setComponent(
                //             entity,
                //             C_Lifetime{randomInt(0, 400)}
                //         );
                // }

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

