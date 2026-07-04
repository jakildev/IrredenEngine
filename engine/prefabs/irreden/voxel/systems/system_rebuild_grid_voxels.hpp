#ifndef SYSTEM_REBUILD_GRID_VOXELS_H
#define SYSTEM_REBUILD_GRID_VOXELS_H

// SYSTEM_REBUILD_GRID_VOXELS — Epic C, C6 (T-294); inverse re-voxelize #1720.
//
// Re-rasterizes a GRID-mode entity's authored voxels into rotated world
// cells. Runs AFTER UPDATE_VOXEL_SET_CHILDREN in the UPDATE pipeline — the
// translate-only path writes a baseline, this system overwrites with the
// entity's full SQT (rotation/scale composed into world cells).
//
// Rotating sets render by DEST-LATTICE INVERSE RESAMPLING (#1720, the CPU
// twin of the detached re-voxelize GPU fix #1619): walk the integer world
// cells of the rotated source AABB, inverse-map each through
// `roundHalfUp(R⁻¹·c)` into a per-set source occupancy grid, and author
// position + color + active per covered cell into the set's pool span.
// Forward scatter (one authored voxel → `roundHalfUp(R·p)`) is not
// surjective onto the covered dest cells — mid-rotation it left up to ~29%
// of a solid 12³ uncovered (the #1720 row/strip holes).
//
// Span contract (#1720 decision, measured across full spins, 2026-06-12):
// the span stays at `numVoxels_` — no allocation change. The covered
// dest-cell count exceeds the span only by a boundary fluctuation (solid
// 12³: ≤ 48 cells ≈ 2.8% observed; solid 16³: ≤ 144 ≈ 3.5%; carved/thin
// shapes allocated as full boxes never exceed it). On overflow, INTERIOR dest
// cells (all 6 neighbors covered) drop first, deterministically in walk
// order — surface cells always render (solid 12³ surface ≈ 732 ≪ span), so
// the cap is invisible for solids. Degenerate exact-fit thin allocations (a
// bare n×n×1 plate) can drop visibly at worst poses (12×12×1: up to 24 of
// 168 covered cells); allocate the full box and carve (the canvas_stress
// orbit pattern) when thin fidelity matters. `dropHighWater_` logs each new
// per-system maximum so overflow stays observable.
//
// While a set rotates, its pool-span colors are a DERIVED arrangement
// (slot i = "covered dest cell i", colors duplicated wherever one source
// voxel covers several dest cells). The authored truth lives in
// `C_VoxelSetNew::rotationSourceVoxels_` — built lazily here on the first
// non-identity frame, mirrored by the component's color mutators, and
// cleared by the identity arm after it restores the span (see the component
// header for the lifecycle contract).
//
// On-screen entities re-rasterize unconditionally each frame (assume
// dynamic). The only work we skip is for entities the camera can't see —
// a cull concern, not a "did the transform change" concern. The system
// queries the canvas voxel pool's per-chunk iso bounds (the same data the
// GPU chunk-visibility mask reads) and skips a voxel set whose pool chunks
// are all outside the cull viewport. There is deliberately NO per-set
// snapshot-compare early-out: a "did this change since last frame"
// side-channel is a dirty flag in disguise (see `cpp-ecs.md` "No dirty
// flags").
//
// Skipped (early returns):
//  - `C_RotationMode::mode_ != GRID` — DETACHED entities rotate through
//    the per-canvas TRS composite (system_entity_canvas_to_framebuffer)
//    and never touch the world voxel pool's positions.
//  - `numVoxels_ <= 0` — headless / pre-canvas staging.
//  - The voxel set's pool chunks are all outside the cull viewport.

#include <cstdint>
#include <span>
#include <unordered_set>
#include <vector>

#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/sun_shadow_constants.hpp>
#include <irreden/render/voxel_pool_allocation.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/face_occupancy.hpp>
#include <irreden/voxel/grid_rotation.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {
template <> struct System<REBUILD_GRID_VOXELS> {
    // Per-tick scratch: last-resolved canvas → pool pointer. Mirrors the
    // same pattern used by UPDATE_VOXEL_SET_CHILDREN — the pool is
    // refetched fresh each tick (canvas archetype migrations between
    // frames invalidate the pointer; within a tick the archetype is
    // stable so amortizing the lookup across multiple voxel-set entities
    // on the same canvas is safe).
    IREntity::EntityId lastCanvas_ = IREntity::kNullEntity;
    C_VoxelPool *lastPool_ = nullptr;

    // Frame-scoped cull viewport, resolved once in beginTick. `cullValid_`
    // is false on the very first frame (before any render populates the
    // cull viewport) — the gate then treats every set as visible.
    IsoBounds2D chunkVp_{};
    bool cullValid_ = false;

    // Identity-arm / fallback exposed-mask scratch, shared with the detached
    // path via IRPrefab::Voxel::recomputeFaceOccupancyOnCells. `cellsScratch_`
    // holds this frame's rounded world destination cells (parallel to the pool
    // sub-span); `occupancyScratch_` is the cell-occupancy set the neighbour
    // probe reads. Members so capacity persists across frames (no per-tick
    // allocation, per cpp-ecs.md "Allocations in hot paths").
    std::vector<ivec3> cellsScratch_;
    std::unordered_set<std::int64_t> occupancyScratch_;

    // Inverse-resample scratch (capacity reused across sets and frames).
    std::vector<std::int32_t> sourceSlotGrid_; // source cell → set-local slot, -1 empty
    std::vector<std::int32_t> destSrcSlot_;    // dest cell → source slot, -1 uncovered
    std::vector<int> destCells_;               // covered dest cells (linear), walk order
    std::vector<int> surfaceCells_;            // covered, ≥1 of 6 neighbors uncovered
    std::vector<int> interiorCells_;           // covered, all 6 neighbors covered

    // Highest per-frame dropped-cell count seen by this system instance.
    // The span-cap drop is expected to be 0 for every shape that allocates
    // its full box; each new maximum logs once (bounded, observable).
    int dropHighWater_ = 0;

    // Dest-walk guard: a pathological scale can explode the rotated-AABB
    // volume. Past this many cells the set falls back to the forward map
    // for the frame (holes over a multi-ms CPU walk + a huge scratch grid).
    static constexpr std::int64_t kMaxDestWalkCells = 2'000'000;

    void beginTick() {
        lastCanvas_ = IREntity::kNullEntity;
        lastPool_ = nullptr;

        // Resolve the cull viewport once per frame, mirroring the chunk
        // visibility gate in system_voxel_to_trixel.hpp. This system runs
        // in the UPDATE pipeline, so getCullViewport() holds the previous
        // frame's render-event snapshot — a one-frame lag that stays
        // consistent with the GPU cull and is invisible for camera pans.
        const IRRender::CullViewportState &cull = IRRender::getCullViewport();
        cullValid_ = cull.canvasSize_.x > 0 && cull.canvasSize_.y > 0;
        if (cullValid_) {
            chunkVp_ = IRPrefab::SunShadow::shadowFeederCullViewport(
                IRRender::kCullChunkMargin,
                IRPrefab::SunShadow::frameShadowFeederParams(),
                cull
            );
        }
    }

    void tick(
        C_VoxelSetNew &voxelSet,
        const C_WorldTransform &worldTransform,
        const C_RotationMode &rotationMode
    ) {
        if (rotationMode.mode_ != RotationMode::GRID) {
            return;
        }
        if (voxelSet.numVoxels_ <= 0) {
            return;
        }

        IREntity::EntityId canvas = voxelSet.canvasEntity_;
        if (canvas == IREntity::kNullEntity) {
            canvas = IRRender::getActiveCanvasEntity();
        }
        if (canvas != lastCanvas_ || lastPool_ == nullptr) {
            lastPool_ = &IREntity::getComponent<C_VoxelPool>(canvas);
            lastCanvas_ = canvas;
        }
        C_VoxelPool &pool = *lastPool_;

        // Cull gate: skip re-rasterization when none of this set's pool
        // chunks are visible. Visible sets re-rasterize every frame.
        if (cullValid_ && !pool.isRangeVisible(
                              voxelSet.voxelStartIdx_,
                              static_cast<std::size_t>(voxelSet.numVoxels_),
                              chunkVp_
                          )) {
            return;
        }

        const std::vector<IRRender::VoxelGpuPosition> &poolPositions = pool.getPositions();
        const std::vector<vec3> &poolOffsets = pool.getPositionOffsets();
        std::vector<IRRender::VoxelGpuPosition> &poolGlobals = pool.getPositionGlobals();
        std::vector<C_Voxel> &poolColors = pool.getColors();

        const size_t baseIdx = voxelSet.voxelStartIdx_;
        const size_t availPositions =
            poolPositions.size() > baseIdx ? poolPositions.size() - baseIdx : 0u;
        const size_t availOffsets =
            poolOffsets.size() > baseIdx ? poolOffsets.size() - baseIdx : 0u;
        const size_t availGlobals =
            poolGlobals.size() > baseIdx ? poolGlobals.size() - baseIdx : 0u;
        const size_t availColors = poolColors.size() > baseIdx ? poolColors.size() - baseIdx : 0u;
        const int safeCount = IRMath::min(
            voxelSet.numVoxels_,
            static_cast<int>(IRMath::min(
                IRMath::min(availPositions, availOffsets),
                IRMath::min(availGlobals, availColors)
            ))
        );
        if (safeCount <= 0) {
            return;
        }

        const bool degenerateScale = worldTransform.scale_.x == 0.0f ||
                                     worldTransform.scale_.y == 0.0f ||
                                     worldTransform.scale_.z == 0.0f;
        if (IRPrefab::GridRotation::isIdentityTransform(worldTransform)) {
            identityArm(voxelSet, worldTransform, pool, baseIdx, safeCount);
        } else if (
            degenerateScale || !inverseArm(voxelSet, worldTransform, pool, baseIdx, safeCount)
        ) {
            // Zero-scale solids have no inverse, and a pathological dest
            // volume is cheaper to render with holes than to walk — both
            // keep the pre-#1720 forward map for the frame.
            forwardArm(voxelSet, worldTransform, pool, baseIdx, safeCount);
        }
    }

    // ---- identity ------------------------------------------------------
    // The pre-#1720 path, byte-identical for a set that never rotated. A
    // set arriving FROM a rotated pose additionally restores its authored
    // span from the snapshot (then clears it — see the component header)
    // and queues the restored positions for upload: the steady-state
    // identity frames after that queue nothing, exactly like master.
    void identityArm(
        C_VoxelSetNew &voxelSet,
        const C_WorldTransform &worldTransform,
        C_VoxelPool &pool,
        size_t baseIdx,
        int safeCount
    ) {
        std::vector<IRRender::VoxelGpuPosition> &poolGlobals = pool.getPositionGlobals();
        const std::vector<IRRender::VoxelGpuPosition> &poolPositions = pool.getPositions();
        const std::vector<vec3> &poolOffsets = pool.getPositionOffsets();
        std::vector<C_Voxel> &poolColors = pool.getColors();

        const bool restored = !voxelSet.rotationSourceVoxels_.empty();
        if (restored) {
            const int m =
                IRMath::min(safeCount, static_cast<int>(voxelSet.rotationSourceVoxels_.size()));
            for (int i = 0; i < m; ++i) {
                poolColors[baseIdx + i] = voxelSet.rotationSourceVoxels_[i];
            }
            voxelSet.rotationSourceVoxels_.clear();
            voxelSet.rotationSourceVoxels_.shrink_to_fit();
            pool.resyncActiveMaskFromColors(baseIdx, static_cast<std::size_t>(safeCount));
        }

        for (int i = 0; i < safeCount; ++i) {
            poolGlobals[baseIdx + i].pos_ = IRPrefab::GridRotation::worldCellForGridVoxel(
                poolPositions[baseIdx + i].pos_,
                poolOffsets[baseIdx + i],
                worldTransform
            );
        }
        // The chunk world-AABB cache must follow the rewritten positions, or
        // the continuous-yaw cull would project a stale (pre-rotation) box
        // and could drop this set's chunks (#1439).
        pool.markChunkWorldBoundsDirty();
        if (restored) {
            pool.queuePositionRange(baseIdx, static_cast<size_t>(safeCount));
        }

        recomputeMaskFromGlobals(pool, baseIdx, safeCount);
    }

    // ---- forward fallback ----------------------------------------------
    // The pre-#1720 forward map for the rare frames the inverse walk is
    // unavailable (zero scale / dest volume past kMaxDestWalkCells). Colors
    // are re-derived from the snapshot when one exists so a set that was
    // mid-spin renders its authored colors, not last frame's dest
    // arrangement; the snapshot is NOT cleared (the set is still rotated).
    void forwardArm(
        C_VoxelSetNew &voxelSet,
        const C_WorldTransform &worldTransform,
        C_VoxelPool &pool,
        size_t baseIdx,
        int safeCount
    ) {
        std::vector<IRRender::VoxelGpuPosition> &poolGlobals = pool.getPositionGlobals();
        const std::vector<IRRender::VoxelGpuPosition> &poolPositions = pool.getPositions();
        const std::vector<vec3> &poolOffsets = pool.getPositionOffsets();
        std::vector<C_Voxel> &poolColors = pool.getColors();

        if (!voxelSet.rotationSourceVoxels_.empty()) {
            const int m =
                IRMath::min(safeCount, static_cast<int>(voxelSet.rotationSourceVoxels_.size()));
            for (int i = 0; i < m; ++i) {
                poolColors[baseIdx + i] = voxelSet.rotationSourceVoxels_[i];
            }
            pool.resyncActiveMaskFromColors(baseIdx, static_cast<std::size_t>(safeCount));
        }

        for (int i = 0; i < safeCount; ++i) {
            poolGlobals[baseIdx + i].pos_ = IRPrefab::GridRotation::worldCellForGridVoxel(
                poolPositions[baseIdx + i].pos_,
                poolOffsets[baseIdx + i],
                worldTransform
            );
        }
        pool.markChunkWorldBoundsDirty();
        pool.queuePositionRange(baseIdx, static_cast<size_t>(safeCount));

        // Same rotated re-voxelize marker the inverse arm sets — this is the
        // forward-map fallback for a ROTATED set, so its cells need the
        // silhouette-riser emit too (component_voxel.hpp VoxelReserved).
        for (int i = 0; i < safeCount; ++i) {
            if (poolColors[baseIdx + i].color_.alpha_ != 0) {
                poolColors[baseIdx + i].reserved_ |= VoxelReserved::kRotatedEmit;
            }
        }

        recomputeMaskFromGlobals(pool, baseIdx, safeCount);
    }

    // ---- inverse resample (#1720) ---------------------------------------
    // Returns false when the dest walk would exceed kMaxDestWalkCells (the
    // caller falls back to the forward map); true on completion.
    bool inverseArm(
        C_VoxelSetNew &voxelSet,
        const C_WorldTransform &worldTransform,
        C_VoxelPool &pool,
        size_t baseIdx,
        int safeCount
    ) {
        std::vector<IRRender::VoxelGpuPosition> &poolGlobals = pool.getPositionGlobals();
        const std::vector<IRRender::VoxelGpuPosition> &poolPositions = pool.getPositions();
        const std::vector<vec3> &poolOffsets = pool.getPositionOffsets();
        std::vector<C_Voxel> &poolColors = pool.getColors();

        // Authored-source snapshot: first rotated frame copies the span
        // (still authored at that moment); the identity arm restores+clears.
        if (voxelSet.rotationSourceVoxels_.empty()) {
            voxelSet.rotationSourceVoxels_.assign(
                poolColors.begin() + static_cast<std::ptrdiff_t>(baseIdx),
                poolColors.begin() + static_cast<std::ptrdiff_t>(baseIdx) + safeCount
            );
        }
        const std::vector<C_Voxel> &source = voxelSet.rotationSourceVoxels_;
        const int sourceCount = IRMath::min(safeCount, static_cast<int>(source.size()));

        // Source occupancy grid over the authored AABB, keyed by
        // roundHalfUp(local + offset) — the same convention the detached
        // GPU sourceGrid_ seed uses, so CPU GRID and GPU detached classify
        // identically. Offsets are live (squash-stretch animates them), so
        // the grid rebuilds per frame; O(span) with reused capacity.
        constexpr int kBig = 1 << 30;
        ivec3 srcMin(kBig, kBig, kBig);
        ivec3 srcMax(-kBig, -kBig, -kBig);
        int activeCount = 0;
        for (int i = 0; i < sourceCount; ++i) {
            if (source[i].color_.alpha_ == 0) {
                continue;
            }
            const ivec3 cell =
                IRMath::roundVec3HalfUp(poolPositions[baseIdx + i].pos_ + poolOffsets[baseIdx + i]);
            srcMin = IRMath::min(srcMin, cell);
            srcMax = IRMath::max(srcMax, cell);
            ++activeCount;
        }
        if (activeCount == 0) {
            // Fully inactive solid — nothing to cover; park the span inactive.
            for (int i = 0; i < safeCount; ++i) {
                poolColors[baseIdx + i].deactivate();
            }
            pool.resyncActiveMaskFromColors(baseIdx, static_cast<std::size_t>(safeCount));
            return true;
        }
        const ivec3 srcDims = srcMax - srcMin + ivec3(1, 1, 1);
        const int srcCellCount = srcDims.x * srcDims.y * srcDims.z;
        sourceSlotGrid_.assign(static_cast<std::size_t>(srcCellCount), -1);
        for (int i = 0; i < sourceCount; ++i) {
            if (source[i].color_.alpha_ == 0) {
                continue;
            }
            const ivec3 g = IRMath::roundVec3HalfUp(
                                poolPositions[baseIdx + i].pos_ + poolOffsets[baseIdx + i]
                            ) -
                            srcMin;
            const int lin = g.x + srcDims.x * (g.y + srcDims.y * g.z);
            if (sourceSlotGrid_[lin] < 0) {
                sourceSlotGrid_[lin] = i; // first-wins: deterministic alias resolution
            }
        }

        // Dest domain: integer world cells inside the rotated source box.
        // The box [srcMin - 0.5, srcMax + 0.5] covers every continuous
        // position that rounds into the source grid; its 8 transformed
        // corners bound all dest candidates (floor/ceil expansion absorbs
        // float error with a one-cell shell).
        const vec3 boxMin = vec3(srcMin) - vec3(0.5f);
        const vec3 boxMax = vec3(srcMax) + vec3(0.5f);
        vec3 aabbMin(kBig);
        vec3 aabbMax(-kBig);
        for (int corner = 0; corner < 8; ++corner) {
            const vec3 local(
                (corner & 1) != 0 ? boxMax.x : boxMin.x,
                (corner & 2) != 0 ? boxMax.y : boxMin.y,
                (corner & 4) != 0 ? boxMax.z : boxMin.z
            );
            const vec3 world =
                worldTransform.translation_ +
                IRMath::rotateVectorByQuat(worldTransform.scale_ * local, worldTransform.rotation_);
            aabbMin = IRMath::min(aabbMin, world);
            aabbMax = IRMath::max(aabbMax, world);
        }
        const ivec3 destMin = ivec3(IRMath::floor(aabbMin));
        const ivec3 destMax = ivec3(IRMath::ceil(aabbMax));
        const ivec3 destDims = destMax - destMin + ivec3(1, 1, 1);
        const std::int64_t destCellCount = static_cast<std::int64_t>(destDims.x) *
                                           static_cast<std::int64_t>(destDims.y) *
                                           static_cast<std::int64_t>(destDims.z);
        if (destCellCount > kMaxDestWalkCells) {
            return false;
        }

        // Inverse walk: one pass over the dest box, recording covered cells
        // in deterministic lex order (x fastest).
        destSrcSlot_.assign(static_cast<std::size_t>(destCellCount), -1);
        destCells_.clear();
        const vec4 inverseRotation = IRMath::quatInverse(worldTransform.rotation_);
        for (int z = 0; z < destDims.z; ++z) {
            for (int y = 0; y < destDims.y; ++y) {
                for (int x = 0; x < destDims.x; ++x) {
                    const ivec3 worldCell = destMin + ivec3(x, y, z);
                    const vec3 composed = IRPrefab::GridRotation::sourceCellForWorldCell(
                        worldCell,
                        worldTransform,
                        inverseRotation
                    );
                    const ivec3 sc = IRMath::roundVec3HalfUp(composed) - srcMin;
                    if (sc.x < 0 || sc.y < 0 || sc.z < 0 || sc.x >= srcDims.x ||
                        sc.y >= srcDims.y || sc.z >= srcDims.z) {
                        continue;
                    }
                    const std::int32_t srcSlot =
                        sourceSlotGrid_[sc.x + srcDims.x * (sc.y + srcDims.y * sc.z)];
                    if (srcSlot < 0) {
                        continue;
                    }
                    const int lin = x + destDims.x * (y + destDims.y * z);
                    destSrcSlot_[lin] = srcSlot;
                    destCells_.push_back(lin);
                }
            }
        }

        // Classify covered cells. A cell on the dest-box boundary is surface
        // by construction (the box bounds the covered set, so the neighbor
        // beyond it is uncovered).
        surfaceCells_.clear();
        interiorCells_.clear();
        const int strideY = destDims.x;
        const int strideZ = destDims.x * destDims.y;
        for (const int lin : destCells_) {
            const int x = lin % destDims.x;
            const int y = (lin / destDims.x) % destDims.y;
            const int z = lin / strideZ;
            const bool interior = x > 0 && x + 1 < destDims.x && destSrcSlot_[lin - 1] >= 0 &&
                                  destSrcSlot_[lin + 1] >= 0 && y > 0 && y + 1 < destDims.y &&
                                  destSrcSlot_[lin - strideY] >= 0 &&
                                  destSrcSlot_[lin + strideY] >= 0 && z > 0 && z + 1 < destDims.z &&
                                  destSrcSlot_[lin - strideZ] >= 0 &&
                                  destSrcSlot_[lin + strideZ] >= 0;
            if (interior) {
                interiorCells_.push_back(lin);
            } else {
                surfaceCells_.push_back(lin);
            }
        }

        // Author the span: surface cells first, then interior while slots
        // remain (the span-cap drop policy above). Face-occlusion bits come
        // from dest-grid adjacency — the rotated-frame generalization #1570
        // introduced; non-face flag bits (AO contrib, emissive) ride along
        // from the source voxel.
        int written = 0;
        auto writeCell = [&](int lin) {
            const int x = lin % destDims.x;
            const int y = (lin / destDims.x) % destDims.y;
            const int z = lin / strideZ;
            const size_t slot = baseIdx + static_cast<size_t>(written);
            poolGlobals[slot].pos_ = vec3(destMin + ivec3(x, y, z));
            C_Voxel out = source[static_cast<std::size_t>(destSrcSlot_[lin])];
            std::uint8_t face = 0u;
            if (x > 0 && destSrcSlot_[lin - 1] >= 0)
                face |= VoxelFlags::kFaceOccludedNegX;
            if (x + 1 < destDims.x && destSrcSlot_[lin + 1] >= 0)
                face |= VoxelFlags::kFaceOccludedPosX;
            if (y > 0 && destSrcSlot_[lin - strideY] >= 0)
                face |= VoxelFlags::kFaceOccludedNegY;
            if (y + 1 < destDims.y && destSrcSlot_[lin + strideY] >= 0)
                face |= VoxelFlags::kFaceOccludedPosY;
            if (z > 0 && destSrcSlot_[lin - strideZ] >= 0)
                face |= VoxelFlags::kFaceOccludedNegZ;
            if (z + 1 < destDims.z && destSrcSlot_[lin + strideZ] >= 0)
                face |= VoxelFlags::kFaceOccludedPosZ;
            out.flags_ =
                static_cast<std::uint8_t>(out.flags_ & ~VoxelFlags::kFaceOccludedMask) | face;
            // Mark this as a rotated re-voxelize cell so the voxel→trixel raster
            // emits the silhouette riser the convex visible-triplet drops on the
            // staircase's grazing edge (the gap fix). Non-rotated sets never reach
            // this arm, so their reserved_ bit stays 0 and the strict-triplet fast
            // path is byte-identical.
            out.reserved_ |= VoxelReserved::kRotatedEmit;
            poolColors[slot] = out;
            ++written;
        };
        for (const int lin : surfaceCells_) {
            if (written >= safeCount) {
                break;
            }
            writeCell(lin);
        }
        for (const int lin : interiorCells_) {
            if (written >= safeCount) {
                break;
            }
            writeCell(lin);
        }
        const int dropped = static_cast<int>(destCells_.size()) - written;
        for (int i = written; i < safeCount; ++i) {
            poolColors[baseIdx + i].deactivate();
        }

        pool.resyncActiveMaskFromColors(baseIdx, static_cast<std::size_t>(safeCount));
        if (written > 0) {
            pool.queuePositionRange(baseIdx, static_cast<size_t>(written));
        }
        pool.markChunkWorldBoundsDirty();

        if (dropped > dropHighWater_) {
            dropHighWater_ = dropped;
            IRE_LOG_WARN(
                "REBUILD_GRID_VOXELS span cap: dropped {} of {} covered dest cells "
                "(span={}, surface={}) — new high-water for this run",
                dropped,
                destCells_.size(),
                safeCount,
                surfaceCells_.size()
            );
        }
        return true;
    }

    // Recompute the exposed-face mask against the ROTATED world cells (#1570
    // GRID parity) for the identity arm and the forward fallback. The
    // authored `C_Voxel.flags_` mask is in the entity's MODEL frame, but the
    // world cells live in WORLD space, so the world-canvas raster
    // (`c_voxel_to_trixel_stage_{1,2}` faceIsExposed) would gate
    // rotated-frame faces against an unrotated mask and drop / mis-colour
    // whole faces — the defect #1570 fixed. The inverse arm derives the same
    // bits directly from its dest grid instead. STAGE_1 re-uploads pool
    // colours (which carry flags_) every frame, so the rewrite reaches the
    // GPU.
    void recomputeMaskFromGlobals(C_VoxelPool &pool, size_t baseIdx, int safeCount) {
        std::vector<C_Voxel> &poolColors = pool.getColors();
        const std::vector<IRRender::VoxelGpuPosition> &poolGlobals = pool.getPositionGlobals();
        if (poolColors.size() < baseIdx + static_cast<size_t>(safeCount) ||
            poolGlobals.size() < baseIdx + static_cast<size_t>(safeCount)) {
            return;
        }
        // Round each slot's world global into its integer destination cell, then
        // let the shared occupancy pass derive the six exposed-face bits — the
        // same helper (and cell packing) system_rebuild_detached_voxels uses, so
        // the GRID identity-arm and the detached re-voxelize path can't drift.
        // Every slot is rounded so cellsScratch_ stays parallel to the pool
        // sub-span [baseIdx, baseIdx + safeCount); inactive voxels contribute no
        // occupancy and are skipped inside recomputeFaceOccupancyOnCells.
        cellsScratch_.resize(static_cast<size_t>(safeCount));
        for (int i = 0; i < safeCount; ++i) {
            cellsScratch_[i] = IRMath::roundVec3HalfUp(poolGlobals[baseIdx + i].pos_);
        }
        IRPrefab::Voxel::recomputeFaceOccupancyOnCells(
            std::span<const ivec3>(cellsScratch_.data(), static_cast<size_t>(safeCount)),
            std::span<C_Voxel>(poolColors.data() + baseIdx, static_cast<size_t>(safeCount)),
            safeCount,
            occupancyScratch_
        );
    }

    static SystemId create() {
        return registerSystem<REBUILD_GRID_VOXELS, C_VoxelSetNew, C_WorldTransform, C_RotationMode>(
            "RebuildGridVoxels"
        );
    }
};
} // namespace IRSystem

#endif /* SYSTEM_REBUILD_GRID_VOXELS_H */
