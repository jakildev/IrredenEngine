#ifndef IR_RENDER_VOXEL_POOL_CONFIG_H
#define IR_RENDER_VOXEL_POOL_CONFIG_H

#include <irreden/ir_math.hpp>

#include <cstdint>
#include <limits>

namespace IRRender::VoxelPoolConfig {

/// Default cube edge length when no Lua override is supplied.
constexpr int kDefaultEdge = 64;

/// Largest accepted cube edge. The binding bound is the per-axis overflow
/// lane (`C_PerAxisTrixelCanvases::overflowCapacityFor`, which asserts it
/// beside `kAxisCount`): `edge³ × 3` face entries must stay within the
/// shader's signed 2^30 field. 710 is the largest edge that fits; it also
/// keeps @ref getTotalSize inside `int`. Enforced by @ref setSize's clamp,
/// which survives `IR_RELEASE`.
constexpr int kMaxEdge = 710;
static_assert(
    static_cast<std::int64_t>(kMaxEdge) * kMaxEdge * kMaxEdge <= std::numeric_limits<int>::max(),
    "kMaxEdge cube must fit getTotalSize's int"
);

/// Set the cube edge length applied to both the global voxel pool and the
/// per-entity allocation cap. Must be called before RenderManager is
/// constructed (i.e. before IREngine::init returns). Values are clamped
/// to [1, @ref kMaxEdge].
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
/// edge length as the global pool.
IRMath::ivec3 getMaxAllocationSize();

/// Total voxels in the global pool (edge³).
int getTotalSize();

/// Total voxels in the per-entity max allocation (edge³). Currently
/// tracks @ref getTotalSize.
int getMaxAllocationSizeTotal();

} // namespace IRRender::VoxelPoolConfig

#endif // IR_RENDER_VOXEL_POOL_CONFIG_H
