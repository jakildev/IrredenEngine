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
// rotation, STAGE_1's chunk-visibility gate can't read it. So this system seeds —
// ONCE — the conservative origin-centered world-AABB (the solid's rotated bounding
// sphere, radius = farthest authored corner from the pool origin) that
// C_VoxelPool::rebuildChunkBounds projects instead of the stale per-voxel globals.
// The bound is rotation-independent, so one seed holds for every spin pose.
//
// It ALSO recomputes the per-voxel exposed-face mask every frame (#1557 P3). The
// model-space `flags_` mask authored at build time is stale once the rotation is
// baked into the cells — it gates the wrong faces (background holes where the
// rotation newly exposed a face, wrong-colored / spurious faces where it buried
// one). We re-derive the mask against the ROTATED destination cells, rotating the
// rigid authored locals about the pool origin exactly as the GPU scatter does —
// the anchored roundHalfUp map of #2349 (GridRotation::anchoredCellForDetachedVoxel,
// CPU↔GPU byte-identical) — so the recomputed mask matches the raster's cells.
// STAGE_1 keeps its standard visible-triplet × exposed-mask gate (no over-emit)
// and uploads the fresh flags on its per-frame color upload. Bounded to the
// SMALL detached pool — not the world pool — so the O(authored voxels) cost P2
// removed for POSITIONS does not return for the world raster.
//
// Ticks the CANVAS entity (it owns the private pool + the reVoxelize_ flag),
// gated on reVoxelize_. Register in the UPDATE pipeline after
// PROPAGATE_CANVAS_ROTATION (sets reVoxelize_ + the current rotation); the
// authored locals it reads are seeded at C_VoxelSetNew construction, so it does
// not depend on UPDATE_VOXEL_SET_CHILDREN having run.
//
// INVARIANT: one centered voxel set per private pool. The conservative bound is
// origin-centered, matching the demo's centered authoring
// (`C_VoxelSetNew(..., centerAroundOrigin=true, ...)`); an off-origin solid would
// need an offset bound. See voxel/CLAUDE.md.

#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/face_occupancy.hpp>
#include <irreden/voxel/grid_rotation.hpp>

#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

namespace IRSystem {

template <> struct System<REBUILD_DETACHED_VOXELS> {
    // Per-frame scratch for the exposed-mask recompute (#1557). Members on the
    // registerSystem instance, so they persist and grow to a high-water mark —
    // the rebuild allocates nothing once warmed (cpp-ecs.md "no allocation in
    // hot ticks"). cellsScratch_ holds this frame's rotated destination cells;
    // occupancyScratch_ is the cell-occupancy set the neighbour probe reads.
    std::vector<IRMath::ivec3> cellsScratch_;
    std::unordered_set<std::int64_t> occupancyScratch_;

    void tick(
        IRComponents::C_VoxelPool &pool, const IRComponents::C_CanvasLocalRotation &canvasRotation
    ) {
        if (!canvasRotation.reVoxelize_) {
            return;
        }
        const int liveCount = pool.getLiveVoxelCount();
        if (liveCount <= 0) {
            return; // pool not filled yet — try again next frame
        }

        const std::vector<IRRender::VoxelGpuPosition> &localPositions = pool.getPositions();
        const std::vector<IRMath::vec3> &localOffsets = pool.getPositionOffsets();
        std::vector<IRComponents::C_Voxel> &colors = pool.getColors();
        const int safeCount = IRMath::min(
            liveCount,
            static_cast<int>(
                IRMath::min(IRMath::min(localPositions.size(), localOffsets.size()), colors.size())
            )
        );
        if (safeCount <= 0) {
            return;
        }

        // Seed the conservative cull bound ONCE — rotation-independent, so it
        // never changes per frame. Per-axis half-extent = farthest authored
        // coordinate from the pool origin (composed = local + offset, the same
        // operand the GPU rotates); setStaticReVoxelizeBound turns this into the
        // origin-centered sphere [-|h|, |h|]^3 (a box of half-extents h reaches
        // its farthest point at |h|), so the bound contains the solid under ANY
        // rotation.
        if (!pool.hasStaticReVoxelizeBound()) {
            IRMath::vec3 halfExtents(0.0f);
            for (int i = 0; i < safeCount; ++i) {
                const IRMath::vec3 composed = localPositions[i].pos_ + localOffsets[i];
                halfExtents = IRMath::max(halfExtents, IRMath::abs(composed));
            }
            pool.setStaticReVoxelizeBound(halfExtents);
        }

        // Recompute the exposed-face mask against the ROTATED destination cells
        // (#1557), through the anchored map the GPU scatter uses
        // (c_revoxelize_detached, #2349): the solid's points sit at
        // cell + anchor, so the dest cell is roundHalfUp(R·composed - anchor)
        // — GridRotation::anchoredCellForDetachedVoxel, byte-identical with the
        // kernel. The anchor is derived here (shared halfCellAnchor, from voxel
        // 0) rather than read from C_DetachedRevoxelizeBuffer::anchor_ because
        // this UPDATE-pipeline tick runs BEFORE the render-side seed on the
        // pool's first frame; seedResidentLocals asserts pool-wide uniformity
        // of the same derivation. The fresh flags_ reach the GPU on STAGE_1's
        // per-frame color upload.
        const IRMath::vec3 anchor = IRPrefab::GridRotation::halfCellAnchor(
            localPositions[0].pos_ + localOffsets[0]
        );
        cellsScratch_.resize(static_cast<std::size_t>(safeCount));
        for (int i = 0; i < safeCount; ++i) {
            cellsScratch_[i] = IRPrefab::GridRotation::anchoredCellForDetachedVoxel(
                localPositions[i].pos_ + localOffsets[i],
                canvasRotation.rotation_,
                anchor
            );
        }
        IRPrefab::Voxel::recomputeFaceOccupancyOnCells(
            std::span<const IRMath::ivec3>(
                cellsScratch_.data(),
                static_cast<std::size_t>(safeCount)
            ),
            std::span<IRComponents::C_Voxel>(colors.data(), static_cast<std::size_t>(safeCount)),
            safeCount,
            occupancyScratch_
        );
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
