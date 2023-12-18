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
    // constexpr uvec2 kGameResolution = uvec2(640, 360);
    constexpr uvec2 kGameResolution = uvec2(1920, 1080);
    // constexpr uvec2 kGameResolution = uvec2(640, 360) / uvec2(2);
    constexpr ivec2 kInitWindowSize = ivec2(1920, 1080);
    // constexpr ivec2 kInitWindowSize = ivec2(
    //     kInitWindowSizeAlt.y / 2,
    //     kInitWindowSizeAlt.x / 2
    // );
    // constexpr ivec2 kInitWindowSize = ivec2(1080, 1920);
    // constexpr uvec2 kGameResolution = uvec2(kInitWindowSize);
    // constexpr uvec2 kGameResolution = uvec2(kInitWindowSize) / uvec2(2);

    constexpr uvec2 kSizeExtraPixelBuffer = uvec2(4, 2);
    constexpr uvec2 kSizeExtraPixelNoBuffer = uvec2(0, 0);

    constexpr uvec2 kGameResolutionWithBuffer =
        kGameResolution +
        kSizeExtraPixelBuffer
    ;

    constexpr uvec2 kScreenCenter = kGameResolution / uvec2(2);

    constexpr uvec3 kChunkSize = uvec3{32, 32, 32};
    constexpr uvec2 kChunkTriangleCanvasSize =
        IRMath::size3DtoSize2DIso(kChunkSize);
    constexpr vec2 kTrixelCanvasZoomMin = vec2{1.0f, 1.0f};
    constexpr vec2 kTrixelCanvasZoomMax = vec2{64.0f, 64.0f};

    constexpr uvec2 kScreenTrixelMaxCanvasSize =
        IRMath::gameResolutionToSize2DIso(
            kGameResolution,
            uvec2(kTrixelCanvasZoomMin)
        );

    constexpr uvec2 kScreenTrixelMaxCanvasSizeWithBuffer =
        IRMath::gameResolutionToSize2DIso(
            kGameResolutionWithBuffer,
            uvec2(kTrixelCanvasZoomMin)
        );
    constexpr uvec2 kScreenTrixelBufferSize =
        kScreenTrixelMaxCanvasSizeWithBuffer -
        kScreenTrixelMaxCanvasSize;
    static_assert(
        kScreenTrixelBufferSize.x == 2 &&
        kScreenTrixelBufferSize.y == 2
    );
    constexpr ivec3 kVoxelPoolPlayerSize = ivec3{16, 16, 16};
    constexpr ivec2 kTrixelCanvasPlayerSize =
        IRMath::size3DtoSize2DIso(kVoxelPoolPlayerSize);

    // Front side faces
    constexpr ivec2 kScreenTrixelOriginOffsetX1 =
        kScreenTrixelMaxCanvasSizeWithBuffer / uvec2(2);
    constexpr ivec2 kScreenTrixelOriginOffsetX2 =
        kScreenTrixelOriginOffsetX1 + ivec2(0, 1);
    constexpr ivec2 kScreenTrixelOriginOffsetY1 =
        kScreenTrixelOriginOffsetX1 + ivec2(-1, 0);
    constexpr ivec2 kScreenTrixelOriginOffsetY2 =
        kScreenTrixelOriginOffsetX1 + ivec2(-1, 1);
    constexpr ivec2 kScreenTrixelOriginOffsetZ1 =
        kScreenTrixelOriginOffsetX1 + ivec2(-1, -1);
    constexpr ivec2 kScreenTrixelOriginOffsetZ2 =
        kScreenTrixelOriginOffsetX1 + ivec2(0, -1);

    // Back side faces (not actually rendered but good for reference)
    constexpr ivec2 kScreenTrixelOriginOffsetX3 = kScreenTrixelOriginOffsetZ1;
    constexpr ivec2 kScreenTrixelOriginOffsetX4 = kScreenTrixelOriginOffsetY1;
    constexpr ivec2 kScreenTrixelOriginOffsetY3 = kScreenTrixelOriginOffsetZ2;
    constexpr ivec2 kScreenTrixelOriginOffsetY4 = kScreenTrixelOriginOffsetX1;
    constexpr ivec2 kScreenTrixelOriginOffsetZ3 = kScreenTrixelOriginOffsetY2;
    constexpr ivec2 kScreenTrixelOriginOffsetZ4 = kScreenTrixelOriginOffsetX2;

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
