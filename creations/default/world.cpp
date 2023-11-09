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
    ivec3 batchSize = ivec3(16, 16, 16);
    auto entities = IRECS::createEntitiesBatchWithFunctions(
        batchSize,
        [batchSize](ivec3 index){
            // IRProfile::logInfo("Index: {}, {}, {}", index.x, index.y, index.z);
            return C_Position3D{
                index
            };
        },
        [batchSize](ivec3 index) {
            Color color{
                roundFloatToByte(static_cast<float>(index.x) / batchSize.x),
                roundFloatToByte(static_cast<float>(index.y) / batchSize.y),
                roundFloatToByte(static_cast<float>(index.z) / batchSize.z),
                255
            };
            // IRProfile::logInfo("Color: {}, {}, {}", color.red_, color.green_, color.blue_);
            return C_VoxelSetNew(
                ivec3(1, 1, 1),
                color
            );
        }
    );

    EntityId parent1 = IRECS::createEntity(
        C_Position3D{0, 24, 0}
    );
    EntityId parent2 = IRECS::createEntity(
        C_Position3D{0, 0, 16}
    );

    int counter = 0;
    for(auto& entity : entities) {
        if(counter % 2 == 0) {
            IRECS::setParent(entity, parent1);

        }
        else {
            IRECS::setParent(entity, parent2);
        }
        counter++;
        // addEntityToScene(entity);
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