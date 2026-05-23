#ifndef IRREDEN_RENDER_VOXEL_POOL_ALLOCATION_H
#define IRREDEN_RENDER_VOXEL_POOL_ALLOCATION_H

#include <irreden/math/ir_math_types.hpp>
#include <irreden/voxel/components/component_voxel.hpp>

#include <cstddef>
#include <span>
#include <type_traits>

namespace IRRender {

// 16-byte std430-stride POD for the per-voxel position SoA arrays in
// C_VoxelPool. The GPU stage-1 dispatch reads
// `kBufferIndex_SingleVoxelPositions` as a tight `vec3` SSBO with std430
// 16-byte stride; the CPU side must match exactly or the per-voxel
// upload mis-strides and every voxel after slot 0 renders at the wrong
// world position. A bare `IRMath::vec3` (12 bytes) cannot back that
// contract — the static_assert below enforces the requirement at
// compile time. `pad_` is uninitialized GPU don't-care padding; the
// renderer never reads it. Voxels never rotate or scale independently
// of their owning `C_VoxelSetNew`, so a full per-slot SQT would
// 2.5×-3.3× the largest hot array (~1M slots) and force a strided GPU
// upload to carry data the shader cannot consume — the architect
// resolved this as Option B on PR #1044; see issue #1054.
struct VoxelGpuPosition {
    IRMath::vec3 pos_;
    float pad_;
};
static_assert(
    sizeof(VoxelGpuPosition) == 16,
    "VoxelGpuPosition must be exactly 16 bytes to match the std430 stride "
    "of the voxel position SSBO (kBufferIndex_SingleVoxelPositions)."
);
static_assert(
    std::is_standard_layout_v<VoxelGpuPosition>,
    "VoxelGpuPosition must be standard-layout for memcpy-style GPU upload."
);

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
// position by `UPDATE_VOXEL_SET_CHILDREN`. Stays raw `IRMath::vec3` —
// CPU-only scratch, never uploaded to the GPU.
struct VoxelPoolAllocation {
    size_t startIndex_ = 0;
    std::span<VoxelGpuPosition> positions_;
    std::span<IRMath::vec3> positionOffsets_;
    std::span<VoxelGpuPosition> positionGlobals_;
    std::span<IRComponents::C_Voxel> voxels_;
};

} // namespace IRRender

#endif /* IRREDEN_RENDER_VOXEL_POOL_ALLOCATION_H */
