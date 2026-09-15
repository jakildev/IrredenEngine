#ifndef SYSTEM_REBUILD_DETACHED_VOXELS_H
#define SYSTEM_REBUILD_DETACHED_VOXELS_H

// Seeds a rotation-independent cull bound for origin-centered private pools.
// Runs after canvas rotation propagation. Allocated source slots, including
// inactive cells, define the bound; carving must not move the rotation origin.

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/grid_rotation.hpp>

#include <vector>

namespace IRSystem {

template <> struct System<REBUILD_DETACHED_VOXELS> {
    void tick(
        IRComponents::C_VoxelPool &pool, const IRComponents::C_CanvasLocalRotation &canvasRotation
    ) {
        if (!canvasRotation.reVoxelize_ || pool.hasStaticReVoxelizeBound()) {
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
        // Both the bound below and the GPU inverse-resample rotate about the
        // POOL ORIGIN and assume it is the body's center. A GROUND- or
        // CORNER-anchored C_VoxelSetNew bakes an asymmetric offset into its
        // composed locals, so it orbits its anchor instead of spinning in
        // place — and the per-voxel halfCellAnchor uniformity assert in
        // seedResidentLocals stays silent, because anchor uniformity is not
        // what breaks (#2911). Checked once per pool lifetime, before the
        // seed, so a failing pool is never given a bound.
        const auto composedAt = [&](int i) { return localPositions[i].pos_ + localOffsets[i]; };
        IR_ASSERT(
            IRPrefab::GridRotation::poolIsOriginCentered(safeCount, composedAt),
            "REBUILD_DETACHED_VOXELS: re-voxelize pool is not origin-centered — "
            "per-axis (min + max) of composed locals = ({},{},{}). A GROUND- or "
            "CORNER-anchored C_VoxelSetNew was allocated into a DETACHED_REVOXELIZE "
            "canvas; the resample rotates about the pool origin, so this solid "
            "would orbit its anchor instead of spinning in place. Author the set "
            "CENTER (#2911).",
            IRPrefab::GridRotation::poolOriginAsymmetry(safeCount, composedAt).x,
            IRPrefab::GridRotation::poolOriginAsymmetry(safeCount, composedAt).y,
            IRPrefab::GridRotation::poolOriginAsymmetry(safeCount, composedAt).z
        );

        IRMath::vec3 halfExtents(0.0f);
        for (int i = 0; i < safeCount; ++i) {
            halfExtents = IRMath::max(halfExtents, IRMath::abs(composedAt(i)));
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
