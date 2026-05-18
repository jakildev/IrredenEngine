#ifndef IR_RENDER_VOXEL_POOL_CONFIG_HPP
#define IR_RENDER_VOXEL_POOL_CONFIG_HPP

#include <irreden/ir_math.hpp>

namespace IRRender::VoxelPoolConfig {

/// Default cube edge length when no CLI override is supplied; preserves
/// the pre-T-277 compile-time pool size.
constexpr int kDefaultEdge = 64;

/// Set the cube edge length applied to both the global voxel pool and the
/// per-entity allocation cap. Must be called before RenderManager is
/// constructed (i.e. before IREngine::init). Values < 1 are clamped to 1.
void setSize(int edge);

/// Cube edge length applied to both the global pool and per-entity
/// allocation cap.
int getEdge();

/// Global voxel pool dimensions (edge × edge × edge).
IRMath::ivec3 getSize();

/// Per-entity max allocation. Currently sized as a cube of the same
/// edge length as the global pool; the issue (#941) leaves room for a
/// separate override in a future task.
IRMath::ivec3 getMaxAllocationSize();

/// Total voxels in the global pool (edge³).
int getTotalSize();

/// Total voxels in the per-entity max allocation (edge³).
int getMaxAllocationSizeTotal();

/// Scan @p argv for `--voxel-pool-size N`; on match, call setSize and log
/// the override. Call from a creation's main() before IREngine::init.
void parseArgv(int argc, char **argv);

} // namespace IRRender::VoxelPoolConfig

#endif // IR_RENDER_VOXEL_POOL_CONFIG_HPP
