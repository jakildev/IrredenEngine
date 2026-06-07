#ifndef SYSTEM_REBUILD_DETACHED_VOXELS_H
#define SYSTEM_REBUILD_DETACHED_VOXELS_H

// SYSTEM_REBUILD_DETACHED_VOXELS — detached re-voxelize conservative-cull-bound
// seeder, #1553 epic P2 (#1556).
//
// P1 (#1555) re-rasterized the private pool's CELL positions on the CPU EVERY
// frame (worldCellForGridVoxel per voxel) and rewrote m_voxelPositionsGlobal. P2
// moves that fill to the GPU scatter (`c_revoxelize_detached`, dispatched from
// VOXEL_TO_TRIXEL_STAGE_1 in place of flushStaticPositionRanges): the GPU compute
// now owns binding 5 on both backends, so the per-frame CPU rewrite is REMOVED —
// it was O(authored voxels), and keeping it would defeat P2's O(entities) goal
// (the only per-frame upload is now the canvas quat).
//
// Consequence the architect flagged: with the CPU mirror no longer following the
// rotation, STAGE_1's chunk-visibility gate can't read it. This system's whole
// remaining job is to seed — ONCE — the conservative origin-centered world-AABB
// (the solid's rotated bounding sphere, radius = farthest authored corner from
// the pool origin) that C_VoxelPool::rebuildChunkBounds projects instead of the
// stale per-voxel globals. The bound is rotation-independent, so one seed holds
// for every spin pose.
//
// Ticks the CANVAS entity (it owns the private pool + the reVoxelize_ flag),
// gated on reVoxelize_; one-time per pool (early-out once the bound is set, so
// there is NO per-frame work after the first seed). Register in the UPDATE
// pipeline after PROPAGATE_CANVAS_ROTATION (sets reVoxelize_); the authored
// locals it reads are seeded at C_VoxelSetNew construction, so it does not depend
// on UPDATE_VOXEL_SET_CHILDREN having run.
//
// INVARIANT: one centered voxel set per private pool. The conservative bound is
// origin-centered, matching the demo's centered authoring
// (`C_VoxelSetNew(..., centerAroundOrigin=true, ...)`); an off-origin solid would
// need an offset bound. See voxel/CLAUDE.md.

#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

namespace IRSystem {

template <> struct System<REBUILD_DETACHED_VOXELS> {
    void tick(
        IRComponents::C_VoxelPool &pool, const IRComponents::C_CanvasLocalRotation &canvasRotation
    ) {
        if (!canvasRotation.reVoxelize_) {
            return;
        }
        // One-time: the conservative bound is rotation-independent, so seed it
        // once and early-out forever after (the rigid authored locals never move).
        if (pool.hasStaticReVoxelizeBound()) {
            return;
        }
        const int liveCount = pool.getLiveVoxelCount();
        if (liveCount <= 0) {
            return; // pool not filled yet — try again next frame
        }

        const std::vector<IRRender::VoxelGpuPosition> &localPositions = pool.getPositions();
        const std::vector<IRMath::vec3> &localOffsets = pool.getPositionOffsets();
        const int safeCount = IRMath::min(
            liveCount,
            static_cast<int>(IRMath::min(localPositions.size(), localOffsets.size()))
        );
        if (safeCount <= 0) {
            return;
        }

        // Per-axis half-extent = farthest authored coordinate from the pool
        // origin (composed = local + offset, the same operand the GPU rotates).
        // setStaticReVoxelizeBound turns this into the origin-centered sphere
        // [-|halfExtents|, |halfExtents|]^3 — a box of half-extents h reaches its
        // farthest point at |h| (= h*sqrt(3) for a cube), so the bound contains
        // the solid under ANY rotation.
        IRMath::vec3 halfExtents(0.0f);
        for (int i = 0; i < safeCount; ++i) {
            const IRMath::vec3 composed = localPositions[i].pos_ + localOffsets[i];
            halfExtents = IRMath::max(halfExtents, IRMath::abs(composed));
        }
        pool.setStaticReVoxelizeBound(halfExtents);
    }

    static SystemId create() {
        return registerSystem<
            REBUILD_DETACHED_VOXELS,
            IRComponents::C_VoxelPool,
            IRComponents::C_CanvasLocalRotation>("RebuildDetachedVoxels");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_REBUILD_DETACHED_VOXELS_H */
