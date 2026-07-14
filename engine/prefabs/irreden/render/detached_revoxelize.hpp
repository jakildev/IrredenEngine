#ifndef IR_PREFAB_DETACHED_REVOXELIZE_H
#define IR_PREFAB_DETACHED_REVOXELIZE_H

// Driver-side lifecycle for the detached re-voxelize GPU scatter's per-pool
// resident locals buffers (#1556, epic #1553 P2). Cross-entity orchestration —
// scan the canvas archetype, lazily allocate + seed each DETACHED_REVOXELIZE
// pool's resident SSBO, report the live set — lives here in a prefab-scoped
// namespace (engine/prefabs/CLAUDE.md Pattern B) so C_DetachedRevoxelizeBuffer
// stays a trivial GPU-RAII holder and VOXEL_TO_TRIXEL_STAGE_1 reaches the buffers
// without a per-entity getComponent (it consumes the reported list, the same
// canvas-keyed-list pattern syncAllocationToCameraYaw uses for the main canvas).

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/buffer.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_detached_revoxelize_buffer.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/grid_rotation.hpp>

#include <cstdint>
#include <utility>
#include <vector>

namespace IRPrefab::DetachedRevoxelize {

namespace detail {

// Seed (or re-seed) the per-pool GPU buffers the re-voxelize fill reads, from
// the pool's RIGID authored locals + per-voxel offsets, composed exactly as the
// CPU worldCellForGridVoxel does before it rotates (`composed = local + offset`).
// Runs once per (re)seed — the locals are rigid, so this is the "GPU owns ongoing
// state, CPU mirror is a one-shot seed" pattern (.claude/rules/cpp-ecs.md), NOT a
// per-frame upload. Seeds three things:
//   1. residentLocals_ — one vec4 per voxel (.xyz = composed) for the IDENTITY
//      fast-path fill (slot == source voxel), unchanged from #1556.
//   2. sourceGrid_ — the dense 3D occupancy+color grid the INVERSE resample
//      (#1619) inverse-looks-up: three uints per source cell ({colorPacked,
//      materialFlagBone, reserved}), keyed by `roundHalfUp(composed) - gridMin`.
//      The third lane carries C_Voxel::reserved_ (per-trixel priority tier,
//      #1960 / #2023) so a ROTATING re-voxelize unit preserves it like a static
//      one — the GPU-side inverse fill authored color without it before, so a
//      spinning detached solid lost its per-trixel depth priority.
//   3. the rotation-independent dest-AABB cube bound (destSide_/destCenter_/
//      destCount_) the inverse fill dispatches + the shared compact walks.
inline void seedResidentLocals(
    IRComponents::C_DetachedRevoxelizeBuffer &buffer, IRComponents::C_VoxelPool &pool, int liveCount
) {
    const std::vector<IRRender::VoxelGpuPosition> &locals = pool.getPositions();
    const std::vector<IRMath::vec3> &offsets = pool.getPositionOffsets();
    const std::vector<IRComponents::C_Voxel> &colors = pool.getColors();
    IR_ASSERT(
        static_cast<int>(locals.size()) >= liveCount &&
            static_cast<int>(offsets.size()) >= liveCount,
        "DetachedRevoxelize: pool locals/offsets smaller than liveCount — pool corruption?"
    );
    const int n =
        IRMath::min(liveCount, static_cast<int>(IRMath::min(locals.size(), offsets.size())));

    // Resident composed locals (identity fast-path) + per-voxel integer cell and
    // origin-centered bound scan for the inverse grid / dest cube. The per-axis
    // half-cell anchor (#2349, GridRotation::halfCellAnchor: -0.5 on even-sized
    // centered axes, 0 on odd) is uniform across the pool — integer authored
    // locals plus ONE shared center-around-origin offset — asserted per voxel so
    // a future non-uniform authoring fails loudly instead of rendering shifted.
    std::vector<IRMath::vec4> staging(static_cast<std::size_t>(n));
    std::vector<IRMath::ivec3> cells(static_cast<std::size_t>(n));
    constexpr int kBig = 1 << 30;
    IRMath::ivec3 gridMin(kBig, kBig, kBig);
    IRMath::ivec3 gridMax(-kBig, -kBig, -kBig);
    float maxRadius = 0.0f;
    IRMath::vec3 anchor(0.0f);
    for (int i = 0; i < n; ++i) {
        const IRMath::vec3 composed = locals[i].pos_ + offsets[i];
        staging[i] = IRMath::vec4(composed, 0.0f);
        const IRMath::ivec3 cell = IRMath::ivec3(IRMath::roundVec3HalfUp(composed));
        cells[i] = cell;
        gridMin = IRMath::min(gridMin, cell);
        gridMax = IRMath::max(gridMax, cell);
        maxRadius = IRMath::max(maxRadius, IRMath::length(composed));
        const IRMath::vec3 residual = IRPrefab::GridRotation::halfCellAnchor(composed);
        if (i == 0) {
            anchor = residual;
            continue;
        }
        const IRMath::vec3 delta = IRMath::abs(residual - anchor);
        IR_ASSERT(
            delta.x < 0.001f && delta.y < 0.001f && delta.z < 0.001f,
            "DetachedRevoxelize: non-uniform half-cell anchor across pool voxels "
            "(({},{},{}) vs ({},{},{})) — the anchored inverse resample assumes "
            "one shared center-around-origin offset",
            residual.x, residual.y, residual.z, anchor.x, anchor.y, anchor.z
        );
    }
    buffer.anchor_ = anchor;
    buffer.residentLocals_.second
        ->subData(0, static_cast<std::size_t>(n) * sizeof(IRMath::vec4), staging.data());

    // Source occupancy+color grid (inverse resample). Dims = source local AABB;
    // three uints per cell ({colorPacked, materialFlagBone, reserved}), zero =
    // empty (alpha byte 0). (Re)allocate only when the cell count grows past the
    // high-water capacity so a re-seed never shrinks.
    const IRMath::ivec3 dims =
        (n > 0) ? (gridMax - gridMin + IRMath::ivec3(1, 1, 1)) : IRMath::ivec3(0, 0, 0);
    const int cellCount = (n > 0) ? dims.x * dims.y * dims.z : 0;
    if (cellCount > buffer.sourceGridCellCapacity_) {
        if (buffer.sourceGrid_.second != nullptr) {
            IRRender::destroyResource<IRRender::Buffer>(buffer.sourceGrid_.first);
        }
        buffer.sourceGrid_ = IRRender::createResource<IRRender::Buffer>(
            nullptr,
            static_cast<std::size_t>(cellCount) * 3 * sizeof(std::uint32_t),
            IRRender::BUFFER_STORAGE_DYNAMIC,
            IRRender::BufferTarget::SHADER_STORAGE,
            IRRender::kBufferIndex_RevoxelizeSourceGrid
        );
        buffer.sourceGridCellCapacity_ = cellCount;
    }
    std::vector<std::uint32_t> grid(static_cast<std::size_t>(cellCount) * 3, 0u);
    const int m = IRMath::min(n, static_cast<int>(colors.size()));
    for (int i = 0; i < m; ++i) {
        const IRMath::ivec3 g = cells[i] - gridMin;
        const int li = g.x + dims.x * (g.y + dims.y * g.z);
        const IRComponents::C_Voxel &v = colors[i];
        grid[static_cast<std::size_t>(li) * 3] = v.color_.toPackedRGBA();
        grid[static_cast<std::size_t>(li) * 3 + 1] =
            static_cast<std::uint32_t>(v.material_id_) |
            (static_cast<std::uint32_t>(v.flags_) << 8) |
            (static_cast<std::uint32_t>(v.bone_id_) << 16) |
            (static_cast<std::uint32_t>(v.layer_id_) << 24);
        // Third lane mirrors the full C_Voxel::reserved_ word (per-trixel
        // priority in bits[1:0]) so MODE 1's GPU-authored dest record carries it
        // exactly like the static binding-6 upload does (#2023).
        grid[static_cast<std::size_t>(li) * 3 + 2] = v.reserved_;
    }
    if (cellCount > 0) {
        buffer.sourceGrid_.second->subData(0, grid.size() * sizeof(std::uint32_t), grid.data());
    }
    buffer.sourceGridMin_ = gridMin;
    buffer.sourceGridDims_ = dims;

    // Dest-AABB cube: enclose the rotated solid under ANY rotation. Rotation
    // preserves length, so the farthest authored corner (maxRadius) bounds every
    // rotated coordinate; the cube [-center, +center]³ holds them all. This is
    // rotation-independent — computed once, valid for every spin pose. The
    // anchored map (#2349) does NOT grow the cube: an anchored axis's dest
    // cells span the same 2·center+1 count shifted +1 cell, which the kernels
    // fold into the slot->cell decode per axis (see revoxDestDecodeShift in
    // c_revoxelize_detached.{glsl,metal}) instead of paying a symmetric grow
    // (a +1 on center costs 11-46% more dispatch/clear/compact work).
    const int center = (n > 0) ? static_cast<int>(IRMath::ceil(maxRadius)) : 0;
    buffer.destCenter_ = center;
    buffer.destSide_ = 2 * center + 1;
    const int rawDestCount = (n > 0) ? (buffer.destSide_ * buffer.destSide_ * buffer.destSide_) : 0;
    const int maxAllocSize = IRRender::VoxelPoolConfig::getMaxAllocationSizeTotal();
    IR_ASSERT(
        rawDestCount <= maxAllocSize,
        "re-voxelize dest cube {} exceeds shared voxel buffer capacity {} — "
        "a private worst-case-sized pool is needed (see #1619 architect note)",
        rawDestCount,
        maxAllocSize
    );
    buffer.destCount_ = rawDestCount;

    buffer.seededVoxelCount_ = liveCount;
}

} // namespace detail

// Allocate + seed the resident locals SSBO for every DETACHED_REVOXELIZE canvas,
// and report the live {canvasEntity, &buffer} set into @p out (cleared first) for
// VOXEL_TO_TRIXEL_STAGE_1's per-entity tick to dispatch against. Idempotent and
// once-per-frame: a steady pool allocates + seeds on the first frame and is a
// pure report thereafter (seededVoxelCount_ already matches liveCount). A pool
// mutation (live-count change) triggers a re-seed; the buffer itself is sized to
// the pool capacity once, so a re-seed never reallocates. Skips non-re-voxelize
// canvases (the main world canvas and forward-scatter detached canvases keep the
// CPU pending-range flush). Called once per frame from
// VOXEL_TO_TRIXEL_STAGE_1::beginTick.
inline void syncResidentBuffers(
    std::vector<std::pair<IREntity::EntityId, IRComponents::C_DetachedRevoxelizeBuffer *>> *out
) {
    if (out != nullptr) {
        out->clear();
    }

    // Dense per-archetype-column iteration (no per-entity getComponent): the
    // three components arrive as parallel column vectors indexed by row.
    const std::vector<IREntity::ArchetypeNode *> nodes = IREntity::queryArchetypeNodesSimple(
        IREntity::getArchetype<
            IRComponents::C_CanvasLocalRotation,
            IRComponents::C_VoxelPool,
            IRComponents::C_DetachedRevoxelizeBuffer>()
    );

    for (IREntity::ArchetypeNode *node : nodes) {
        std::vector<IRComponents::C_CanvasLocalRotation> &rotations =
            IREntity::getComponentData<IRComponents::C_CanvasLocalRotation>(node);
        std::vector<IRComponents::C_VoxelPool> &pools =
            IREntity::getComponentData<IRComponents::C_VoxelPool>(node);
        std::vector<IRComponents::C_DetachedRevoxelizeBuffer> &buffers =
            IREntity::getComponentData<IRComponents::C_DetachedRevoxelizeBuffer>(node);

        for (int i = 0; i < node->length_; ++i) {
            if (!rotations[i].reVoxelize_) {
                continue;
            }
            IRComponents::C_VoxelPool &pool = pools[i];
            IRComponents::C_DetachedRevoxelizeBuffer &buffer = buffers[i];
            const int liveCount = pool.getLiveVoxelCount();
            if (liveCount <= 0) {
                continue; // pool not filled yet — nothing to seed or dispatch
            }

            if (!buffer.isAllocated()) {
                // Size to the pool's full capacity once so a later re-seed never
                // reallocates. One vec4 per slot (.xyz = composed local).
                const int capacity = pool.getVoxelPoolSize();
                auto resource = IRRender::createResource<IRRender::Buffer>(
                    nullptr,
                    static_cast<std::size_t>(capacity) * sizeof(IRMath::vec4),
                    IRRender::BUFFER_STORAGE_DYNAMIC,
                    IRRender::BufferTarget::SHADER_STORAGE,
                    IRRender::kBufferIndex_LocalVoxelPositions
                );
                buffer.residentLocals_ = resource;
                buffer.capacity_ = capacity;
                buffer.seededVoxelCount_ = -1;
            }

            // Seed once; re-seed only when the live count changes (pool mutation),
            // never per frame — a per-frame re-seed would revert the path to
            // O(authored voxels), the exact trap the resource model exists to
            // avoid.
            if (buffer.seededVoxelCount_ != liveCount) {
                detail::seedResidentLocals(buffer, pool, liveCount);
            }

            if (out != nullptr) {
                out->emplace_back(node->entities_[i], &buffer);
            }
        }
    }
}

// Cap the re-voxelize single-canvas subdivision density (`voxelRenderOptions.y`)
// so the model-space lattice stays inside the entity's fixed trixel canvas
// (#1570 D2 — the single-canvas analogue of the per-axis #1431 cap in
// IRPrefab::PerAxisCanvas::subdivisionDensity). A re-voxelize canvas rasters its
// pool in model space (camera yaw + pan zeroed) and is sized once to the pool's
// rotated-AABB iso footprint at base resolution — it does NOT scale with effSub.
// effSub folds in camera zoom (`clamp(m_vrs × round(zoom), 1, 16)`), so at
// zoom > 1 the iso footprint × effSub overflows the fixed canvas and
// `isInsideCanvas` silently drops the on-screen cells (the bottom/edge clip in
// #1570). Unlike the per-axis cap — whose on-screen extent is the camera
// viewport / zoom — the detached model-space footprint is zoom-independent, so
// the cap is a pure function of canvas size + pool 3D bounds.
//
// The capped value is read by the compact pass (it sizes the indirect dispatch's
// Z count = subdivisions²) AND by `c_voxel_to_trixel_stage_{1,2}`, so the caller
// must apply it BEFORE the per-canvas UBO upload + compact dispatch — then all
// three read the same value and no surplus-invocation skip guard is needed (the
// per-axis path needs one only because it caps AFTER the compact already sized
// the dispatch from the uncapped effSub). Returns ≥ 1.
inline int subdivisionCap(IRMath::ivec2 canvasSize, IRMath::ivec3 poolSize3D) {
    // iso-pixel spread of the pool's 3D bounds at effSub == 1. The spread is
    // translation-invariant, so the pool origin is irrelevant — only the extent
    // matters, and the rotated solid lives inside this axis-aligned box.
    const IRMath::IsoBounds2D iso = IRMath::entityIsoBounds(IRMath::vec3(0.0f), poolSize3D);
    const IRMath::vec2 footprint = iso.max_ - iso.min_;
    const int capX = static_cast<int>(
        IRMath::floor(static_cast<float>(canvasSize.x) / IRMath::max(footprint.x, 1.0f))
    );
    const int capY = static_cast<int>(
        IRMath::floor(static_cast<float>(canvasSize.y) / IRMath::max(footprint.y, 1.0f))
    );
    return IRMath::max(IRMath::min(capX, capY), 1);
}

} // namespace IRPrefab::DetachedRevoxelize

#endif /* IR_PREFAB_DETACHED_REVOXELIZE_H */
