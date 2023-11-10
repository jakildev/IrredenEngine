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
    // TODO: Change these to config settings and remove from here
    constexpr ivec2 kInitWindowSize = ivec2(1080, 1920);
    // constexpr ivec2 kInitWindowSize = ivec2(1080, 1920) / ivec2(2);
    // constexpr ivec2 kInitWindowSize = ivec2(360, 640);

    // constexpr uvec2 kGameResolution = uvec2(640, 360);
    constexpr uvec2 kGameResolution = uvec2(360, 640);
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
    constexpr vec2 kTriangleCanvasZoomMin = vec2{1.0f, 1.0f};
    constexpr vec2 kTriangleCanvasZoomMax = vec2{64.0f, 64.0f};

    constexpr uvec2 kScreenTriangleMaxCanvasSize =
        IRMath::gameResolutionToSize2DIso(
            kGameResolution,
            kTriangleCanvasZoomMin
        );

    constexpr uvec2 kScreenTriangleMaxCanvasSizeWithBuffer =
        IRMath::gameResolutionToSize2DIso(
            kGameResolutionWithBuffer,
            kTriangleCanvasZoomMin
        );
    constexpr uvec2 kScreenTriangleBufferSize =
        kScreenTriangleMaxCanvasSizeWithBuffer -
        kScreenTriangleMaxCanvasSize;
    constexpr ivec3 kVoxelPoolPlayerSize = ivec3{16, 16, 16};
    constexpr ivec2 kTriangleCanvasPlayerSize =
        IRMath::size3DtoSize2DIso(kVoxelPoolPlayerSize);

    // Front side faces
    constexpr ivec2 kScreenTriangleOriginOffsetX1 =
        kScreenTriangleMaxCanvasSizeWithBuffer / uvec2(2);
    constexpr ivec2 kScreenTriangleOriginOffsetX2 =
        kScreenTriangleOriginOffsetX1 + ivec2(0, 1);
    constexpr ivec2 kScreenTriangleOriginOffsetY1 =
        kScreenTriangleOriginOffsetX1 + ivec2(-1, 0);
    constexpr ivec2 kScreenTriangleOriginOffsetY2 =
        kScreenTriangleOriginOffsetX1 + ivec2(-1, 1);
    constexpr ivec2 kScreenTriangleOriginOffsetZ1 =
        kScreenTriangleOriginOffsetX1 + ivec2(-1, -1);
    constexpr ivec2 kScreenTriangleOriginOffsetZ2 =
        kScreenTriangleOriginOffsetX1 + ivec2(0, -1);

    // Back side faces (not actually rendered but good for reference)
    constexpr ivec2 kScreenTriangleOriginOffsetX3 = kScreenTriangleOriginOffsetZ1;
    constexpr ivec2 kScreenTriangleOriginOffsetX4 = kScreenTriangleOriginOffsetY1;
    constexpr ivec2 kScreenTriangleOriginOffsetY3 = kScreenTriangleOriginOffsetZ2;
    constexpr ivec2 kScreenTriangleOriginOffsetY4 = kScreenTriangleOriginOffsetX1;
    constexpr ivec2 kScreenTriangleOriginOffsetZ3 = kScreenTriangleOriginOffsetY2;
    constexpr ivec2 kScreenTriangleOriginOffsetZ4 = kScreenTriangleOriginOffsetX2;

    constexpr ivec3 kChunkGroundOrigin = ivec3{0, 0, kChunkSize.z - 1};

    constexpr vec3 kWorldBoundMin = vec3{0, 0, 0};
    constexpr vec3 kWorldBoundMax = vec3(kChunkSize) - vec3(1, 1, 2);

    constexpr Color kColorBlack = Color{0, 0, 0, 255};
    constexpr Color kColorGreen = Color{0, 255, 0, 255};
    constexpr Color kColorRed = Color{255, 0, 0, 255};
    constexpr Color kColorBlue = Color{0, 0, 255, 255};
    constexpr Color kColorWhite = Color{255, 255, 255, 255};
    constexpr Color kColorYellow = Color{255, 255, 0, 255};
    constexpr Color kColorMagenta = Color{255, 0, 255, 255};
    constexpr Color kColorCyan = Color{0, 255, 255, 255};
    constexpr Color kColorBlackTransparent = Color{0, 0, 0, 50};

    constexpr Distance kTriangleDistanceMinDistance =       -65535;
    constexpr Distance kTriangleDistanceMaxDistance =       65535;

    // TODO: Dynamic based on current GPU
    constexpr ivec3 kVoxelPoolMaxAllocationSize = ivec3{64, 64, 64};
    constexpr int kVoxelPoolMaxAllocationSizeTotal =
        kVoxelPoolMaxAllocationSize.x *
        kVoxelPoolMaxAllocationSize.y *
        kVoxelPoolMaxAllocationSize.z;

    constexpr ivec3 kVoxelPoolSize = ivec3{64, 64, 64};

} // namespace constants

#endif /* CONSTANTS_H */
