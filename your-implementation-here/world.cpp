#include "world.hpp"

#include <entities/entity_single_voxel.hpp>

#include <components/component_position_3d.hpp>
#include <components/component_voxel_set.hpp>

World::World(int argc, char **argv)
:   IRWorld(argc, argv)
{
    GAME_LOG_INFO("Creating world: XxxxxXxxxx");
}

World::~World()
{

}

void World::initGameEntities()
{
    EntityHandle testBlock{};
    testBlock.set(C_Position3D{});
    testBlock.set(C_VoxelSetNew{
        ivec3{4, 4, 4}
    });
    addEntityToScene(testBlock);
}

void World::initGameSystems()
{

}