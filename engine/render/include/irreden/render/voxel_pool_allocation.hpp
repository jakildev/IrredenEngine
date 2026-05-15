#ifndef IRREDEN_RENDER_VOXEL_POOL_ALLOCATION_H
#define IRREDEN_RENDER_VOXEL_POOL_ALLOCATION_H

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/math/ir_math_types.hpp>
#include <irreden/voxel/components/component_voxel.hpp>

#include <cstddef>
#include <span>

namespace IRRender {

// Result of @c IRRender::allocateVoxels (and @c RenderManager::allocateVoxels /
// @c C_VoxelPool::allocateVoxels). @c startIndex_ is the source of truth for
// where the spans live inside the pool's voxel arrays — consumers must not
// recompute it from `positions.data() - basePtr`. The caller's cached basePtr
// can refer to a different pool's allocation (e.g. when an EntityId-keyed
// pool-pointer cache survives a canvas-archetype mutation), so the
// pointer-diff produces an out-of-bounds index that on macOS walks off into
// unmapped VM space.
//
// `positionOffsets_` is per-voxel scratch storage authored by voxel-set
// deformation systems (`VOXEL_SQUASH_STRETCH`) and summed into the global
// position by `UPDATE_VOXEL_SET_CHILDREN`. This is internal pool state,
// not the engine-level entity offset channel — entity offsets travel
// through the modifier framework (`POSITION_OFFSET_3D` vec3 field) and
// land on `C_PositionGlobal3D` directly.
struct VoxelPoolAllocation {
    size_t startIndex_ = 0;
    std::span<IRComponents::C_Position3D> positions_;
    std::span<IRMath::vec3> positionOffsets_;
    std::span<IRComponents::C_PositionGlobal3D> positionGlobals_;
    std::span<IRComponents::C_Voxel> voxels_;
};

} // namespace IRRender

#endif /* IRREDEN_RENDER_VOXEL_POOL_ALLOCATION_H */
