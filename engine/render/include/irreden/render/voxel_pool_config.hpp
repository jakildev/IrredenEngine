#ifndef IR_RENDER_VOXEL_POOL_CONFIG_H
#define IR_RENDER_VOXEL_POOL_CONFIG_H

#include <irreden/ir_math.hpp>

namespace IRRender::VoxelPoolConfig {

/// Default cube edge length when no Lua override is supplied; preserves
/// the pre-T-277 compile-time pool size.
constexpr int kDefaultEdge = 64;

/// Set the cube edge length applied to both the global voxel pool and the
/// per-entity allocation cap. Must be called before RenderManager is
/// constructed (i.e. before IREngine::init returns). Values < 1 are
/// clamped to 1.
///
/// In a normal creation the canonical pre-init pass in
/// `IREngine::init` reads `config.voxel_pool_edge` from `config.lua` and
/// calls `setSize` on the demo's behalf — see
/// `engine/world/CLAUDE.md` "Init-affecting runtime params" for the
/// pattern. Direct calls remain valid for tests or other contexts that
/// don't run through `IREngine::init`.
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

/// Total voxels in the per-entity max allocation (edge³). Currently
/// tracks `getTotalSize()`; the issue (#941) leaves room for a separate
/// per-entity total once the residency manager work introduces an
/// independent per-entity override.
int getMaxAllocationSizeTotal();

} // namespace IRRender::VoxelPoolConfig

#endif // IR_RENDER_VOXEL_POOL_CONFIG_H
