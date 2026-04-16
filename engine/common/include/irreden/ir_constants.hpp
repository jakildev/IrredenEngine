#ifndef IR_CONSTANTS_H
#define IR_CONSTANTS_H

#include <irreden/ir_math.hpp>

using namespace IRMath;

namespace IRConstants {

/// Target frame rate for the main fixed-step game loop (frames per second).
constexpr int kFPS = 60;

/// Extra pixel margin added to the main framebuffer and trixel canvas beyond
/// the nominal game resolution.  Prevents edge-clipping artefacts at canvas
/// borders when the view is at a chunk boundary.
constexpr ivec2 kSizeExtraPixelBuffer = uvec2(4, 2);

/// Voxel chunk edge length in each dimension.  All chunks are 32×32×32.
constexpr uvec3 kChunkSize = uvec3{32, 32, 32};
/// Trixel-canvas dimensions for one full chunk, derived from kChunkSize via
/// the isometric projection.
constexpr uvec2 kChunkTriangleCanvasSize = IRMath::size3DtoSize2DIso(kChunkSize);

/// Minimum scale factor for trixel canvas rendering.
constexpr vec2 kTrixelCanvasZoomMin = vec2{1.0f, 1.0f};
/// Maximum scale factor for trixel canvas rendering.
constexpr vec2 kTrixelCanvasZoomMax = vec2{64.0f, 64.0f};

/// Voxel pool dimensions reserved for the player entity.
constexpr ivec3 kVoxelPoolPlayerSize = ivec3{16, 16, 16};
/// Trixel-canvas dimensions for the player voxel pool, derived via the
/// isometric projection.
constexpr ivec2 kTrixelCanvasPlayerSize = IRMath::size3DtoSize2DIso(kVoxelPoolPlayerSize);

/// World-space 3D origin of a chunk's ground layer (z = kChunkSize.z - 1).
constexpr ivec3 kChunkGroundOrigin = ivec3{0, 0, kChunkSize.z - 1};

/// Minimum world-coordinate bound (inclusive).
constexpr vec3 kWorldBoundMin = vec3{0, 0, 0};
/// Maximum world-coordinate bound (inclusive).  The z ceiling is
/// kChunkSize.z - 2, leaving the top ground layer as the scene floor.
constexpr vec3 kWorldBoundMax = vec3(kChunkSize) - vec3(1, 1, 2);

/// Trixel depth near-plane sentinel.  Values at or below this depth render on
/// top of all scene geometry (used by GUI text, for example).
constexpr Distance kTrixelDistanceMinDistance = -65535;
/// Per-frame trixel depth clear value.  The distance texture is cleared to
/// this value before each frame; shaders write strictly smaller values via
/// imageAtomicMin.  Acts as the "nothing here" background depth.
constexpr Distance kTrixelDistanceMaxDistance = 65535;

/// Maximum voxel-pool allocation per entity (x × y × z voxels).
/// TODO: derive from available GPU VRAM rather than using a fixed budget.
constexpr ivec3 kVoxelPoolMaxAllocationSize = ivec3{64, 64, 64};
/// Total voxels in the maximum per-entity allocation
/// (kVoxelPoolMaxAllocationSize.x × .y × .z).
constexpr int kVoxelPoolMaxAllocationSizeTotal =
    kVoxelPoolMaxAllocationSize.x * kVoxelPoolMaxAllocationSize.y * kVoxelPoolMaxAllocationSize.z;

/// Global voxel pool dimensions (x × y × z voxels).
/// TODO: initialise from GPU stats; support multiple pools for GPUs smaller
/// than this default.
constexpr ivec3 kVoxelPoolSize = ivec3{64, 64, 64};
/// Total voxels in the global pool (kVoxelPoolSize product).
constexpr int kMaxSingleVoxels = IRMath::multVecComponents(IRConstants::kVoxelPoolSize);

} // namespace IRConstants

#endif /* CONSTANTS_H */
