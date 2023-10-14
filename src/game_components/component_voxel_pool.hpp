/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_voxel_pool.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_VOXEL_POOL_H
#define COMPONENT_VOXEL_POOL_H

// Not sure if this is used rn, might get rid of it

#include "../math/ir_math.hpp"
#include <vector>
#include <span>
using namespace IRMath;

namespace IRComponents {

    // Could also mean root node of a scene?
    // I should do this while I am here
    struct C_VoxelPool {
        int numVoxels_;
        ivec3 size_;
        // CPU buffers
        std::span<C_Position3D> positions_;
        std::span<C_PositionOffset3D> positionOffsets_; // WIP
        std::span<C_PositionGlobal3D> positionGlobals_;
        std::span<C_Voxel> voxels_;

        // default constructor
        C_VoxelPool()
        {

        }
    };
} // namespace IRComponents

#endif /* COMPONENT_VOXEL_POOL_H */