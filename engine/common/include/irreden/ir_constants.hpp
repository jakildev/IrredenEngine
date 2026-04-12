#ifndef IR_CONSTANTS_H
#define IR_CONSTANTS_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRConstants {

/// Target frame rate for the fixed-step update loop.
constexpr int kFPS = 60;

/// Extra canvas dimensions (in iso pixels) added beyond the game resolution.
/// Prevents visible seams at canvas edges when the camera is offset by a
/// fractional amount — the trixel-to-framebuffer blit can shift by up to
/// this amount without uncovering empty canvas.
constexpr ivec2 kSizeExtraPixelBuffer = uvec2(4, 2);

/// Size of a standard voxel chunk (width × depth × height, in voxels).
constexpr uvec3 kChunkSize = uvec3{32, 32, 32};

/// Iso-space canvas size (in triangles) that covers one full chunk at 1:1 zoom.
/// Derived from kChunkSize via the isometric projection.
constexpr uvec2 kChunkTriangleCanvasSize = IRMath::size3DtoSize2DIso(kChunkSize);

/// Minimum zoom level for the trixel canvas (1 iso-pixel = 1 screen pixel).
constexpr vec2 kTrixelCanvasZoomMin = vec2{1.0f, 1.0f};

/// Maximum zoom level for the trixel canvas (1 iso-pixel = 64 screen pixels).
constexpr vec2 kTrixelCanvasZoomMax = vec2{64.0f, 64.0f};

/// Reserved: intended voxel-pool bounding box for a player entity.
/// Currently unused — kept as a reference size for future player-specific pools.
constexpr ivec3 kVoxelPoolPlayerSize = ivec3{16, 16, 16};

/// Iso-space canvas size for a player voxel pool (derived from kVoxelPoolPlayerSize).
/// Currently unused.
constexpr ivec2 kTrixelCanvasPlayerSize = IRMath::size3DtoSize2DIso(kVoxelPoolPlayerSize);

/// World-space origin for entities placed at ground level within a chunk.
/// Z is the maximum layer (kChunkSize.z - 1) because the walkable surface
/// is the topmost voxel layer of the chunk — higher Z is visually higher.
constexpr ivec3 kChunkGroundOrigin = ivec3{0, 0, kChunkSize.z - 1};

/// Minimum corner of the valid world-space AABB for entity positions.
constexpr vec3 kWorldBoundMin = vec3{0, 0, 0};

/// Maximum corner of the valid world-space AABB for entity positions.
/// Leaves a 1-voxel border on X/Y and a 2-voxel border on Z to keep entities
/// inside visible canvas area.
constexpr vec3 kWorldBoundMax = vec3(kChunkSize) - vec3(1, 1, 2);

/// Sentinel value used to clear / mark "empty" in the trixel distance buffer.
/// Anything ≤ this is treated as background (behind every real voxel).
constexpr Distance kTrixelDistanceMinDistance = -65535;

/// Maximum depth value stored in the trixel distance buffer.
/// Used as the initial clear value and as a far-plane sentinel in shaders.
constexpr Distance kTrixelDistanceMaxDistance = 65535;

/// Maximum voxel pool AABB per entity allocation.
/// TODO: derive this dynamically from GPU memory stats at init time.
constexpr ivec3 kVoxelPoolMaxAllocationSize = ivec3{64, 64, 64};

/// Total voxel count in one max-size entity allocation.
constexpr int kVoxelPoolMaxAllocationSizeTotal =
    kVoxelPoolMaxAllocationSize.x * kVoxelPoolMaxAllocationSize.y * kVoxelPoolMaxAllocationSize.z;

/// Total size of the GPU voxel pool (all single-voxel entity slots combined).
/// TODO: initialise based on GPU stats; make multiple pools if needed.
constexpr ivec3 kVoxelPoolSize = ivec3{64, 64, 64};

/// Total number of single-voxel slots in the GPU pool.
constexpr int kMaxSingleVoxels = IRMath::multVecComponents(IRConstants::kVoxelPoolSize);

} // namespace IRConstants

#endif /* CONSTANTS_H */
