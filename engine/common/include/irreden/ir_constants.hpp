/*
 * Project: Irreden Engine
 * File: ir_constants.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_CONSTANTS_H
#define IR_CONSTANTS_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRConstants {

    constexpr int kFPS = 60;

    constexpr uvec2 kSizeExtraPixelBuffer = uvec2(4, 2);

    constexpr uvec3 kChunkSize = uvec3{32, 32, 32};
    constexpr uvec2 kChunkTriangleCanvasSize =
        IRMath::size3DtoSize2DIso(kChunkSize);
    constexpr vec2 kTrixelCanvasZoomMin = vec2{1.0f, 1.0f};
    constexpr vec2 kTrixelCanvasZoomMax = vec2{64.0f, 64.0f};

    constexpr ivec3 kVoxelPoolPlayerSize = ivec3{16, 16, 16};
    constexpr ivec2 kTrixelCanvasPlayerSize =
        IRMath::size3DtoSize2DIso(kVoxelPoolPlayerSize);

    constexpr ivec3 kChunkGroundOrigin = ivec3{0, 0, kChunkSize.z - 1};

    constexpr vec3 kWorldBoundMin = vec3{0, 0, 0};
    constexpr vec3 kWorldBoundMax = vec3(kChunkSize) - vec3(1, 1, 2);

    constexpr Distance kTrixelDistanceMinDistance =       -65535;
    constexpr Distance kTrixelDistanceMaxDistance =       65535;

    // TODO: Dynamic based on current GPU
    constexpr ivec3 kVoxelPoolMaxAllocationSize = ivec3{32, 32, 32};
    constexpr int kVoxelPoolMaxAllocationSizeTotal =
        kVoxelPoolMaxAllocationSize.x *
        kVoxelPoolMaxAllocationSize.y *
        kVoxelPoolMaxAllocationSize.z;

    constexpr ivec3 kVoxelPoolSize = ivec3{32, 32, 32};
    // TODO: initalize buffer based on GPU stats, and make multiple to
    // make up the difference
    constexpr int kMaxSingleVoxels =
        IRMath::multVecComponents(IRConstants::kVoxelPoolSize);

} // namespace constants

#endif /* CONSTANTS_H */
