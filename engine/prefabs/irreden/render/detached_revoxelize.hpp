#ifndef IR_PREFAB_DETACHED_REVOXELIZE_H
#define IR_PREFAB_DETACHED_REVOXELIZE_H

// Driver-side lifecycle for the detached re-voxelize GPU scatter's per-pool
// resident locals buffers. Cross-entity orchestration —
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
#include <irreden/voxel/face_occupancy.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_set>
#include <utility>
#include <vector>

namespace IRPrefab::DetachedRevoxelize {

namespace detail {

// Source-indexed uploads need masks in the current rotated lattice. GPU inverse
// resampling owns destination-indexed masks and must bypass this CPU work.
inline void recomputeSourceFaceOccupancy(
    IRComponents::C_VoxelPool &pool,
    IRMath::vec4 rotation,
    std::vector<IRMath::ivec3> &cells,
    std::unordered_set<std::int64_t> &occupancy
) {
    const auto &positions = pool.getPositions();
    const auto &offsets = pool.getPositionOffsets();
    auto &colors = pool.getColors();
    const int count = IRMath::min(
        pool.getLiveVoxelCount(),
        static_cast<int>(IRMath::min(IRMath::min(positions.size(), offsets.size()), colors.size()))
    );
    if (count <= 0) {
        return;
    }
    const IRMath::vec3 anchor =
        IRPrefab::GridRotation::halfCellAnchor(positions[0].pos_ + offsets[0]);
    cells.resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        cells[i] = IRPrefab::GridRotation::anchoredCellForDetachedVoxel(
            positions[i].pos_ + offsets[i],
            rotation,
            anchor
        );
    }
    IRPrefab::Voxel::recomputeFaceOccupancyOnCells(
        std::span<const IRMath::ivec3>(cells.data(), static_cast<std::size_t>(count)),
        std::span<IRComponents::C_Voxel>(colors.data(), static_cast<std::size_t>(count)),
        count,
        occupancy
    );
}

// Seed one cell group from the pool span [start, start + count): scan its
// composed locals for the integer source cells, the source-grid bounds, the
// origin-centered radius and the half-cell anchor. The per-axis half-cell
// anchor (GridRotation::halfCellAnchor: -0.5 on even-sized centered axes, 0 on
// odd) is uniform across a rigid set — integer authored locals plus ONE shared
// center-around-origin offset — asserted per voxel so non-uniform authoring
// fails loudly instead of rendering shifted. @p cells receives the span's
// source cells for the grid fill that follows.
inline IRComponents::RevoxelizeGroupSeed scanGroupSpan(
    const IRComponents::C_VoxelPool &pool,
    std::size_t start,
    std::size_t count,
    std::vector<IRMath::ivec3> &cells
) {
    const std::vector<IRRender::VoxelGpuPosition> &locals = pool.getPositions();
    const std::vector<IRMath::vec3> &offsets = pool.getPositionOffsets();
    constexpr int kBig = 1 << 30;
    IRMath::ivec3 gridMin(kBig, kBig, kBig);
    IRMath::ivec3 gridMax(-kBig, -kBig, -kBig);
    float maxRadius = 0.0f;
    IRMath::vec3 anchor(0.0f);
    cells.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        const IRMath::vec3 composed = locals[start + i].pos_ + offsets[start + i];
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

    IRComponents::RevoxelizeGroupSeed seed{};
    seed.spanStart_ = start;
    seed.spanCount_ = count;
    seed.gridMin_ = gridMin;
    seed.gridDims_ = gridMax - gridMin + IRMath::ivec3(1, 1, 1);
    seed.anchor_ = anchor;
    // Dest cube: enclose the rotated solid under ANY rotation. Rotation
    // preserves length, so the farthest authored corner (maxRadius) bounds
    // every rotated coordinate. Rotation-independent — computed once, valid for
    // every spin pose.
    seed.destCenter_ = static_cast<int>(IRMath::ceil(maxRadius));
    seed.destSide_ = 2 * seed.destCenter_ + 1;
    return seed;
}

// The pool spans a re-voxelize fill resamples: the posted cell groups, or one
// implicit group over the live prefix for a pool that never hosted any.
inline void collectGroupSpans(
    const IRComponents::C_VoxelPool &pool,
    int liveCount,
    std::vector<std::pair<std::size_t, std::size_t>> &spans
) {
    spans.clear();
    if (!pool.hostsCellGroups()) {
        if (liveCount > 0) {
            spans.emplace_back(0u, static_cast<std::size_t>(liveCount));
        }
        return;
    }
    for (const IRComponents::VoxelCellGroup &group : pool.getCellGroups()) {
        spans.emplace_back(group.start_, group.count_);
    }
}

// True when @p buffer was seeded from exactly @p spans. The span set changes
// only when a hosted set joins or leaves the pool, so a steady pool never
// re-seeds.
inline bool seededFromSpans(
    const IRComponents::C_DetachedRevoxelizeBuffer &buffer,
    const std::vector<std::pair<std::size_t, std::size_t>> &spans
) {
    if (buffer.groups_.size() != spans.size()) {
        return false;
    }
    for (std::size_t i = 0; i < spans.size(); ++i) {
        if (buffer.groups_[i].spanStart_ != spans[i].first ||
            buffer.groups_[i].spanCount_ != spans[i].second) {
            return false;
        }
    }
    return true;
}

// Seed (or re-seed) the per-pool GPU buffers the re-voxelize fill reads, from
// the pool's RIGID authored locals + per-voxel offsets, composed exactly as the
// CPU worldCellForGridVoxel does before it rotates (`composed = local + offset`).
// Runs once per (re)seed — the locals are rigid, so this is the "GPU owns ongoing
// state, CPU mirror is a one-shot seed" pattern (.claude/rules/cpp-ecs.md), NOT a
// per-frame upload. Seeds three things:
//   1. residentLocals_ — one vec4 per voxel (.xyz = composed) for the IDENTITY
// fast-path fill (slot == source voxel).
//   2. sourceGrid_ — per cell group, the dense 3D occupancy+color grid the
//      INVERSE resample inverse-looks-up: three uints per source cell
//      ({colorPacked, materialFlagBone, reserved}), keyed by
//      `roundHalfUp(composed) - gridMin`. The third lane carries
//      C_Voxel::reserved_ (per-trixel priority tier), so a ROTATING re-voxelize
//      unit preserves it like a static one — the GPU-side inverse fill authored
//      color without it before, so a spinning detached solid lost its
//      per-trixel depth priority.
//   3. the rotation-independent dest cube of each group and the contiguous
//      dest-slot range it fills; their total is what the inverse fill
//      dispatches + the shared compact walks.
inline void seedResidentLocals(
    IRComponents::C_DetachedRevoxelizeBuffer &buffer,
    IRComponents::C_VoxelPool &pool,
    int liveCount,
    const std::vector<std::pair<std::size_t, std::size_t>> &spans
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

    std::vector<IRMath::vec4> staging(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        staging[i] = IRMath::vec4(locals[i].pos_ + offsets[i], 0.0f);
    }
    buffer.residentLocals_.second
        ->subData(0, static_cast<std::size_t>(n) * sizeof(IRMath::vec4), staging.data());

    // Scan every group first: the concatenated grid is sized from all of them.
    const bool explicitGroups = pool.hostsCellGroups();
    std::vector<std::vector<IRMath::ivec3>> groupCells(spans.size());
    buffer.groups_.clear();
    buffer.groups_.reserve(spans.size());
    int cellCount = 0;
    int destCount = 0;
    for (std::size_t g = 0; g < spans.size(); ++g) {
        const std::size_t start = spans[g].first;
        const std::size_t live = static_cast<std::size_t>(n);
        const std::size_t count = start < live ? IRMath::min(spans[g].second, live - start) : 0u;
        IRComponents::RevoxelizeGroupSeed seed = scanGroupSpan(pool, start, count, groupCells[g]);
        // A hosted set rotates about its own entity origin. An off-center set
        // would orbit that origin instead of spinning in place; the implicit
        // single group is checked by REBUILD_DETACHED_VOXELS before it seeds
        // the cull bound.
        IR_ASSERT(
            !explicitGroups ||
                IRPrefab::GridRotation::poolIsOriginCentered(
                    static_cast<int>(count),
                    [&](int i) { return locals[start + i].pos_ + offsets[start + i]; }
                ),
            "DetachedRevoxelize: the hosted voxel set at span {} is not centered on its "
            "entity origin. Author canvas parts CENTER.",
            start
        );
        seed.gridWordBase_ = cellCount * 3;
        seed.destSlotBase_ = destCount;
        cellCount += seed.gridDims_.x * seed.gridDims_.y * seed.gridDims_.z;
        destCount += seed.destSide_ * seed.destSide_ * seed.destSide_;
        buffer.groups_.push_back(seed);
    }

    // (Re)allocate only when the cell count grows past the high-water capacity
    // so a re-seed never shrinks.
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
    // Zero = empty (alpha byte 0).
    std::vector<std::uint32_t> grid(static_cast<std::size_t>(cellCount) * 3, 0u);
    for (std::size_t g = 0; g < buffer.groups_.size(); ++g) {
        const IRComponents::RevoxelizeGroupSeed &seed = buffer.groups_[g];
        const std::size_t m = IRMath::min(
            seed.spanCount_,
            colors.size() - IRMath::min(seed.spanStart_, colors.size())
        );
        for (std::size_t i = 0; i < m; ++i) {
            const IRMath::ivec3 cell = groupCells[g][i] - seed.gridMin_;
            const std::size_t word =
                static_cast<std::size_t>(seed.gridWordBase_) +
                static_cast<std::size_t>(
                    cell.x + seed.gridDims_.x * (cell.y + seed.gridDims_.y * cell.z)
                ) * 3;
            const IRComponents::C_Voxel &v = colors[seed.spanStart_ + i];
            grid[word] = v.color_.toPackedRGBA();
            grid[word + 1] = static_cast<std::uint32_t>(v.material_id_) |
                             (static_cast<std::uint32_t>(v.flags_) << 8) |
                             (static_cast<std::uint32_t>(v.bone_id_) << 16) |
                             (static_cast<std::uint32_t>(v.layer_id_) << 24);
            // Third lane mirrors the full C_Voxel::reserved_ word (per-trixel
            // priority in bits[1:0]) so MODE 1's GPU-authored dest record
            // carries it exactly like the static binding-6 upload does.
            grid[word + 2] = v.reserved_;
        }
    }
    if (cellCount > 0) {
        buffer.sourceGrid_.second->subData(0, grid.size() * sizeof(std::uint32_t), grid.data());
    }

    const int maxAllocSize = IRRender::VoxelPoolConfig::getMaxAllocationSizeTotal();
    IR_ASSERT(
        destCount <= maxAllocSize,
        "re-voxelize dest cubes {} exceed shared voxel buffer capacity {} — "
        "a private worst-case-sized pool is needed (see #1619 architect note)",
        destCount,
        maxAllocSize
    );
    buffer.destCount_ = destCount;
    buffer.anchor_ = buffer.groups_.empty() ? IRMath::vec3(0.0f) : buffer.groups_.front().anchor_;
    buffer.seededVoxelCount_ = liveCount;
}

} // namespace detail

// First dest cell, per axis, of the cube that holds a group of radius
// `destCenter` translated by @p translation, on a lattice whose cells sit at
// `cell + phase`. A point p = cell + phase - translation belongs to the group
// when |p| <= destCenter, so the covered cells are
// [ceil(u - destCenter), floor(u + destCenter)] with u = translation - phase —
// at most `2·destCenter + 1` of them starting here. With no translation this
// is `-destCenter` on an un-anchored axis and one cell higher on an anchored
// (phase -0.5) one, which keeps the anchored axes at zero dispatch growth.
inline IRMath::ivec3 destWindowBase(IRMath::vec3 translation, IRMath::vec3 phase, int destCenter) {
    return IRMath::ivec3(IRMath::ceil(translation - phase)) - IRMath::ivec3(destCenter);
}

// GPU descriptor of one cell group at this frame's pose. @p phase is the
// canvas lattice phase every group of the pool rasters on.
inline IRRender::RevoxelizeGroupParams groupParams(
    const IRComponents::RevoxelizeGroupSeed &seed,
    IRMath::vec4 rotation,
    IRMath::vec3 translation,
    IRMath::vec3 phase
) {
    IRRender::RevoxelizeGroupParams params{};
    params.rotation_ = rotation;
    params.destOffset_ = IRMath::vec4(phase - translation, 0.0f);
    params.anchor_ = IRMath::vec4(seed.anchor_, 0.0f);
    params.destBase_ =
        IRMath::ivec4(destWindowBase(translation, phase, seed.destCenter_), seed.destSide_);
    params.srcGridMin_ = IRMath::ivec4(seed.gridMin_, seed.destSlotBase_);
    params.srcGridDims_ = IRMath::ivec4(seed.gridDims_, seed.gridWordBase_);
    return params;
}

// Allocate + seed the resident locals SSBO for every DETACHED_REVOXELIZE canvas,
// and report the live {canvasEntity, &buffer} set into @p out (cleared first) for
// VOXEL_TO_TRIXEL_STAGE_1's per-entity tick to dispatch against. Idempotent and
// once-per-frame: a steady pool allocates + seeds on the first frame and is a
// pure report thereafter. A pool mutation — a live-count change, or a hosted
// set joining or leaving — triggers a re-seed; the locals buffer itself is
// sized to the pool capacity once, so a re-seed never reallocates it. Skips
// non-re-voxelize canvases (the main world canvas and forward-scatter detached
// canvases keep the CPU pending-range flush). Called once per frame from
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

    std::vector<std::pair<std::size_t, std::size_t>> spans;
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

            // Seed once; re-seed only when the pool's hosted spans change,
            // never per frame — a per-frame re-seed would revert the path to
            // O(authored voxels), the exact trap the resource model exists to
            // avoid.
            detail::collectGroupSpans(pool, liveCount, spans);
            if (buffer.seededVoxelCount_ != liveCount || !detail::seededFromSpans(buffer, spans)) {
                detail::seedResidentLocals(buffer, pool, liveCount, spans);
            }

            if (out != nullptr) {
                out->emplace_back(node->entities_[i], &buffer);
            }
        }
    }
}

// Cap the re-voxelize single-canvas subdivision density (`voxelRenderOptions.y`)
// so the model-space lattice stays inside the entity's fixed trixel canvas
// — the single-canvas analogue of IRPrefab::PerAxisCanvas::subdivisionDensity.
// A re-voxelize canvas rasters its
// pool in model space (camera yaw + pan zeroed) and is sized once to the pool's
// rotated-AABB iso footprint at base resolution — it does NOT scale with effSub.
// effSub folds in camera zoom (`clamp(m_vrs × round(zoom), 1, 16)`), so at
// zoom > 1 the iso footprint × effSub overflows the fixed canvas and
// `isInsideCanvas` silently drops the on-screen cells at the bottom or edge.
// Unlike the per-axis cap, whose on-screen extent is the camera
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
