#ifndef SYSTEM_REBUILD_GRID_VOXELS_H
#define SYSTEM_REBUILD_GRID_VOXELS_H

// SYSTEM_REBUILD_GRID_VOXELS — re-rasterizes GRID-mode entities via inverse
// re-voxelize.
//
// Re-rasterizes a GRID-mode entity's authored voxels into rotated world
// cells. Runs AFTER UPDATE_VOXEL_SET_CHILDREN in the UPDATE pipeline — the
// translate-only path writes a baseline, this system overwrites with the
// entity's full SQT (rotation/scale composed into world cells).
//
// Rotating sets render by DEST-LATTICE INVERSE RESAMPLING (the CPU
// twin of the detached re-voxelize GPU path): walk the integer world
// cells of the rotated source AABB, inverse-map each through
// `roundHalfUp(R⁻¹·c)` into a per-set source occupancy grid, and author
// position + color + active per covered cell into the set's pool span.
// Forward scatter (one authored voxel → `roundHalfUp(R·p)`) is not
// surjective onto the covered dest cells — mid-rotation it leaves up to ~29%
// of a solid 12³ uncovered (row/strip holes).
//
// Destination-capacity contract: a pose must emit every EXPOSED dest cell
// (at least one of 6 neighbors uncovered); INTERIOR cells fill whatever
// allocation remains and may drop, deterministically in walk order. The set's
// allocation (`allocatedVoxels_`) starts at its authored count. When a pose's
// exposed count exceeds it, the set grows once to a monotonic high-water mark
// — 3/2 of the current allocation, else the exact exposed count — bounded by
// the pool size and kMaxDestWalkCells; it never grows for interior cells, so
// a solid 12³ (610 exposed of 1740 covered) stays at 1728 slots, while an
// exact-fit 12×12×1 plate at 45° (145 exposed) grows to 216. A pose the pool
// cannot admit, or whose dest box exceeds kMaxDestWalkCells, fails closed:
// the whole allocation goes inactive for the frame, the authored records stay
// intact, and the set retries every frame until a pose fits
// (`unadmittedDestinationCells_`). Nothing ever emits a truncated surface.
// `capacityStats_` counts growths and failures; each new per-system maximum
// of interior drops or of unadmitted cells logs once.
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
// Entities WITHOUT `C_RotationMode` are implicitly GRID and are covered by
// the twin arm `REBUILD_GRID_VOXELS_IMPLICIT` in this same header — register
// both wherever this one registers.
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

    // Highest per-frame dropped-interior-cell count seen by this system
    // instance; each new maximum logs once (bounded, observable).
    int dropHighWater_ = 0;

    // Dest-walk guard: a pathological scale can explode the rotated-AABB
    // volume. Past this many cells the pose fails closed for the frame rather
    // than paying a multi-ms CPU walk and a huge scratch grid. It also caps
    // admission: no set grows past it.
    static constexpr std::int64_t kMaxDestWalkCells = 2'000'000;

    enum class AdmissionFailure : std::uint8_t {
        NONE = 0,
        CAPACITY,
        WALK_GUARD,
    };

    // Destination-capacity telemetry for probes (read through
    // `getSystemParams`). `last*` describe the most recent failed admission.
    struct CapacityStats {
        std::uint64_t growths_ = 0;
        std::uint64_t failures_ = 0;
        int peakCapacity_ = 0;
        AdmissionFailure lastFailure_ = AdmissionFailure::NONE;
        int lastAuthored_ = 0;
        std::int64_t lastRequired_ = 0;
        int lastCapacity_ = 0;
        int lastBudget_ = 0;
    };
    CapacityStats capacityStats_;
    std::int64_t unadmittedHighWater_ = 0;

    // Largest allocation any set in @p pool may grow to.
    static int admissionBudget(const C_VoxelPool &pool) {
        return static_cast<int>(
            IRMath::min(kMaxDestWalkCells, static_cast<std::int64_t>(pool.getVoxelPoolSize()))
        );
    }

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
        if (!voxelSet.renders() || voxelSet.numVoxels_ <= 0) {
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

        // Cull gate over the whole allocation: skip re-rasterization when
        // none of this set's pool chunks are visible. A fail-closed set has
        // no live voxels to bound, so it bypasses the gate until a pose is
        // admitted again.
        if (cullValid_ && voxelSet.unadmittedDestinationCells_ == 0 &&
            !pool.isRangeVisible(
                voxelSet.voxelStartIdx_,
                static_cast<std::size_t>(voxelSet.allocatedVoxels_),
                chunkVp_
            )) {
            return;
        }

        // The four pool vectors share the pool size; an allocation that does
        // not fit inside it is corrupt, never partially rebuilt.
        if (voxelSet.allocatedVoxels_ < voxelSet.numVoxels_ ||
            voxelSet.voxelStartIdx_ + static_cast<std::size_t>(voxelSet.allocatedVoxels_) >
                pool.getColors().size()) {
            return;
        }

        const bool degenerateScale = worldTransform.scale_.x == 0.0f ||
                                     worldTransform.scale_.y == 0.0f ||
                                     worldTransform.scale_.z == 0.0f;
        if (IRPrefab::GridRotation::isIdentityTransform(worldTransform)) {
            identityArm(voxelSet, worldTransform, pool);
        } else if (degenerateScale) {
            // Zero-scale solids have no inverse; the forward map collapses
            // them onto a plane without dropping any authored voxel.
            forwardArm(voxelSet, worldTransform, pool);
        } else {
            inverseArm(voxelSet, worldTransform, pool);
        }
    }

    // Deactivate the output-only tail [numVoxels_, allocatedVoxels_), which
    // only a rotated pose populates.
    static void deactivateTail(C_VoxelSetNew &voxelSet, std::vector<C_Voxel> &poolColors) {
        for (int i = voxelSet.numVoxels_; i < voxelSet.allocatedVoxels_; ++i) {
            poolColors[voxelSet.voxelStartIdx_ + static_cast<std::size_t>(i)].deactivate();
        }
    }

    // Park the whole allocation inactive, authored slots and tail alike.
    static void deactivateAllocation(C_VoxelSetNew &voxelSet, C_VoxelPool &pool) {
        std::vector<C_Voxel> &poolColors = pool.getColors();
        const size_t baseIdx = voxelSet.voxelStartIdx_;
        const std::size_t capacity = static_cast<std::size_t>(voxelSet.allocatedVoxels_);
        for (std::size_t i = 0; i < capacity; ++i) {
            poolColors[baseIdx + i].deactivate();
        }
        pool.applyFogCarrierPolicy(std::span<C_Voxel>{poolColors.data() + baseIdx, capacity});
        pool.resyncActiveMaskFromColors(baseIdx, capacity);
    }

    // ---- identity ------------------------------------------------------
    // The identity path, byte-identical for a set that never rotated. A
    // set arriving FROM a rotated pose additionally restores its authored
    // span from the snapshot (then clears it — see the component header),
    // deactivates the output-only tail while keeping its capacity for later
    // poses, and queues the restored positions for upload: the steady-state
    // identity frames after that queue nothing.
    void identityArm(
        C_VoxelSetNew &voxelSet, const C_WorldTransform &worldTransform, C_VoxelPool &pool
    ) {
        std::vector<IRRender::VoxelGpuPosition> &poolGlobals = pool.getPositionGlobals();
        const std::vector<IRRender::VoxelGpuPosition> &poolPositions = pool.getPositions();
        const std::vector<vec3> &poolOffsets = pool.getPositionOffsets();
        std::vector<C_Voxel> &poolColors = pool.getColors();
        const size_t baseIdx = voxelSet.voxelStartIdx_;
        const int authoredCount = voxelSet.numVoxels_;
        const std::size_t capacity = static_cast<std::size_t>(voxelSet.allocatedVoxels_);

        voxelSet.unadmittedDestinationCells_ = 0;
        const bool restored = !voxelSet.rotationSourceVoxels_.empty();
        if (restored) {
            const int m =
                IRMath::min(authoredCount, static_cast<int>(voxelSet.rotationSourceVoxels_.size()));
            for (int i = 0; i < m; ++i) {
                poolColors[baseIdx + i] = voxelSet.rotationSourceVoxels_[i];
            }
            deactivateTail(voxelSet, poolColors);
            pool.applyFogCarrierPolicy(std::span<C_Voxel>{poolColors.data() + baseIdx, capacity});
            voxelSet.rotationSourceVoxels_.clear();
            voxelSet.rotationSourceVoxels_.shrink_to_fit();
            pool.resyncActiveMaskFromColors(baseIdx, capacity);
        }

        for (int i = 0; i < authoredCount; ++i) {
            poolGlobals[baseIdx + i].pos_ = IRPrefab::GridRotation::worldCellForGridVoxel(
                poolPositions[baseIdx + i].pos_,
                poolOffsets[baseIdx + i],
                worldTransform
            );
        }
        // Positions change on every identity frame, so both cull caches need
        // invalidation even when no GPU restore upload is required.
        pool.markCullBoundsDirty(baseIdx, capacity);
        if (restored) {
            pool.queuePositionRange(baseIdx, static_cast<size_t>(authoredCount));
        }

        recomputeMaskFromGlobals(pool, baseIdx, authoredCount);
    }

    // ---- forward map (zero scale) --------------------------------------
    // A zero-scale pose has no inverse. Every authored voxel maps forward,
    // so nothing is dropped; the output-only tail goes inactive. Colors are
    // re-derived from the snapshot when one exists so a set that was mid-spin
    // renders its authored colors, not last frame's dest arrangement; the
    // snapshot is NOT cleared (the set is still rotated).
    void
    forwardArm(C_VoxelSetNew &voxelSet, const C_WorldTransform &worldTransform, C_VoxelPool &pool) {
        std::vector<IRRender::VoxelGpuPosition> &poolGlobals = pool.getPositionGlobals();
        const std::vector<IRRender::VoxelGpuPosition> &poolPositions = pool.getPositions();
        const std::vector<vec3> &poolOffsets = pool.getPositionOffsets();
        std::vector<C_Voxel> &poolColors = pool.getColors();
        const size_t baseIdx = voxelSet.voxelStartIdx_;
        const int authoredCount = voxelSet.numVoxels_;
        const std::size_t capacity = static_cast<std::size_t>(voxelSet.allocatedVoxels_);

        voxelSet.unadmittedDestinationCells_ = 0;
        if (!voxelSet.rotationSourceVoxels_.empty()) {
            const int m =
                IRMath::min(authoredCount, static_cast<int>(voxelSet.rotationSourceVoxels_.size()));
            for (int i = 0; i < m; ++i) {
                poolColors[baseIdx + i] = voxelSet.rotationSourceVoxels_[i];
            }
        }
        deactivateTail(voxelSet, poolColors);
        pool.resyncActiveMaskFromColors(baseIdx, capacity);

        for (int i = 0; i < authoredCount; ++i) {
            poolGlobals[baseIdx + i].pos_ = IRPrefab::GridRotation::worldCellForGridVoxel(
                poolPositions[baseIdx + i].pos_,
                poolOffsets[baseIdx + i],
                worldTransform
            );
        }
        pool.markCullBoundsDirty(baseIdx, capacity);
        pool.queuePositionRange(baseIdx, static_cast<size_t>(authoredCount));

        // Same rotated re-voxelize marker the inverse arm sets — this is the
        // forward-map fallback for a ROTATED set, so its cells need the
        // silhouette-riser emit too (component_voxel.hpp VoxelReserved).
        for (int i = 0; i < authoredCount; ++i) {
            if (poolColors[baseIdx + i].color_.alpha_ != 0) {
                poolColors[baseIdx + i].reserved_ |= VoxelReserved::kRotatedEmit;
            }
        }
        pool.applyFogCarrierPolicy(
            std::span<C_Voxel>{poolColors.data() + baseIdx, static_cast<size_t>(authoredCount)}
        );

        recomputeMaskFromGlobals(pool, baseIdx, authoredCount);
    }

    // ---- admission ---------------------------------------------------------
    // Grow @p voxelSet so it holds @p required destination cells: 3/2 of the
    // current allocation first, then the exact requirement when the
    // speculative span does not fit. False when neither can be admitted.
    bool admitCapacity(C_VoxelSetNew &voxelSet, C_VoxelPool &pool, int required) {
        const int budget = admissionBudget(pool);
        if (required > budget) {
            return false;
        }
        const int current = voxelSet.allocatedVoxels_;
        const int speculative = IRMath::min(budget, IRMath::max(required, current + current / 2));
        const bool grown = voxelSet.growAllocation(pool, speculative) ||
                           (speculative != required && voxelSet.growAllocation(pool, required));
        if (grown) {
            ++capacityStats_.growths_;
            capacityStats_.peakCapacity_ =
                IRMath::max(capacityStats_.peakCapacity_, voxelSet.allocatedVoxels_);
        }
        return grown;
    }

    // An unadmitted pose renders nothing: the whole allocation goes inactive
    // (the child-position baseline already rewrote the authored slots this
    // frame, so a partially refreshed prior pose must not survive) while the
    // authored records stay in `rotationSourceVoxels_`.
    void failClosed(
        C_VoxelSetNew &voxelSet, C_VoxelPool &pool, AdmissionFailure reason, std::int64_t required
    ) {
        deactivateAllocation(voxelSet, pool);
        voxelSet.unadmittedDestinationCells_ = static_cast<int>(
            IRMath::max(std::int64_t{1}, IRMath::min(required, std::int64_t{kMaxDestWalkCells}))
        );
        ++capacityStats_.failures_;
        capacityStats_.lastFailure_ = reason;
        capacityStats_.lastAuthored_ = voxelSet.numVoxels_;
        capacityStats_.lastRequired_ = required;
        capacityStats_.lastCapacity_ = voxelSet.allocatedVoxels_;
        capacityStats_.lastBudget_ = admissionBudget(pool);
        if (required > unadmittedHighWater_) {
            unadmittedHighWater_ = required;
            IRE_LOG_ERROR(
                "REBUILD_GRID_VOXELS: {} — pose not admitted, set renders nothing this frame "
                "(authored={}, required={}, capacity={}, budget={}, pool resident={}) — new "
                "high-water for this run",
                reason == AdmissionFailure::WALK_GUARD ? "dest walk guard" : "capacity",
                voxelSet.numVoxels_,
                required,
                voxelSet.allocatedVoxels_,
                capacityStats_.lastBudget_,
                pool.residentSlotCount()
            );
        }
    }

    // ---- inverse resample ------------------------------------------------
    void
    inverseArm(C_VoxelSetNew &voxelSet, const C_WorldTransform &worldTransform, C_VoxelPool &pool) {
        std::vector<IRRender::VoxelGpuPosition> &poolGlobals = pool.getPositionGlobals();
        const std::vector<IRRender::VoxelGpuPosition> &poolPositions = pool.getPositions();
        const std::vector<vec3> &poolOffsets = pool.getPositionOffsets();
        std::vector<C_Voxel> &poolColors = pool.getColors();
        size_t baseIdx = voxelSet.voxelStartIdx_;
        const int authoredCount = voxelSet.numVoxels_;

        // Authored-source snapshot: first rotated frame copies the span
        // (still authored at that moment); the identity arm restores+clears.
        if (voxelSet.rotationSourceVoxels_.empty()) {
            voxelSet.rotationSourceVoxels_.assign(
                poolColors.begin() + static_cast<std::ptrdiff_t>(baseIdx),
                poolColors.begin() + static_cast<std::ptrdiff_t>(baseIdx) + authoredCount
            );
        }
        const std::vector<C_Voxel> &source = voxelSet.rotationSourceVoxels_;
        const int sourceCount = IRMath::min(authoredCount, static_cast<int>(source.size()));

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
            deactivateAllocation(voxelSet, pool);
            voxelSet.unadmittedDestinationCells_ = 0;
            return;
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
            failClosed(voxelSet, pool, AdmissionFailure::WALK_GUARD, destCellCount);
            return;
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

        // Admit every exposed cell before writing anything. Growth moves the
        // span, so the base index is re-read afterwards.
        const int exposed = static_cast<int>(surfaceCells_.size());
        if (exposed > voxelSet.allocatedVoxels_ && !admitCapacity(voxelSet, pool, exposed)) {
            failClosed(voxelSet, pool, AdmissionFailure::CAPACITY, exposed);
            return;
        }
        voxelSet.unadmittedDestinationCells_ = 0;
        baseIdx = voxelSet.voxelStartIdx_;
        const int capacity = voxelSet.allocatedVoxels_;

        // Author the allocation: surface cells first, then interior while
        // slots remain (interior cells are the only ones that drop). Face-occlusion bits are
        // derived from dest-grid adjacency, never copied: a source voxel's
        // bits describe its neighbours in the source lattice, which the
        // rotation does not preserve. Every other flag bit (AO contrib,
        // emissive) is copied from the source voxel.
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
            // staircase's grazing edge. Non-rotated sets never reach this arm, so
            // their reserved_ bit stays 0 and the strict-triplet fast path is
            // byte-identical.
            out.reserved_ |= VoxelReserved::kRotatedEmit;
            poolColors[slot] = out;
            ++written;
        };
        for (const int lin : surfaceCells_) {
            writeCell(lin);
        }
        for (const int lin : interiorCells_) {
            if (written >= capacity) {
                break;
            }
            writeCell(lin);
        }
        const int dropped = static_cast<int>(destCells_.size()) - written;
        for (int i = written; i < capacity; ++i) {
            poolColors[baseIdx + i].deactivate();
        }
        pool.applyFogCarrierPolicy(
            std::span<C_Voxel>{poolColors.data() + baseIdx, static_cast<size_t>(capacity)}
        );

        pool.resyncActiveMaskFromColors(baseIdx, static_cast<std::size_t>(capacity));
        if (written > 0) {
            pool.queuePositionRange(baseIdx, static_cast<size_t>(written));
        }
        // Include the deactivated tail: position uploads cannot carry alpha edits.
        pool.markCullBoundsDirty(baseIdx, static_cast<std::size_t>(capacity));

        if (dropped > dropHighWater_) {
            dropHighWater_ = dropped;
            IRE_LOG_WARN(
                "REBUILD_GRID_VOXELS: dropped {} interior of {} covered dest cells "
                "(capacity={}, surface={}) — new high-water for this run",
                dropped,
                destCells_.size(),
                capacity,
                surfaceCells_.size()
            );
        }
    }

    // Recompute the exposed-face mask against the ROTATED world cells for
    // the identity arm and the forward fallback. The
    // authored `C_Voxel.flags_` mask is in the entity's MODEL frame, but the
    // world cells live in WORLD space, so the world-canvas raster
    // (`c_voxel_to_trixel_stage_{1,2}` faceIsExposed) would gate
    // rotated-frame faces against an unrotated mask and drop / mis-colour
    // whole faces. The inverse arm derives the same
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

// REBUILD_GRID_VOXELS_IMPLICIT — the same re-rasterize for entities that
// carry NO C_RotationMode.
//
// `component_rotation_mode.hpp` documents absence of the component as
// implicitly GRID; this arm is what makes that true of the re-rasterize,
// whose own archetype requires the component. Without it a component-less
// entity gets only the translate-only baseline from UPDATE_VOXEL_SET_CHILDREN
// and its authored rotation/scale renders as identity, silently — and the
// rotation drivers (AUTO_SPIN_LOCAL_TRANSFORM, ROTATION_TARGET_LOCAL_TRANSFORM)
// query neither C_RotationMode nor C_VoxelSetNew, so nothing upstream notices.
//
// The two systems partition the population: `Exclude<C_RotationMode>` here
// vs. the required `C_RotationMode` on the main arm. No entity is ever ticked
// twice, and an entity that gains or loses the component (`setMode`) migrates
// archetypes and switches arms on its own — no special handling.
//
// This spec owns a `System<REBUILD_GRID_VOXELS>` instance and delegates, so
// the inverse / identity / forward arms and all of their reused scratch
// capacity stay in exactly one place. The delegate's scratch is per-instance,
// so the two systems never share buffers.
//
// The `detail::` free-function shape the sibling include/exclude twin
// (MODIFIER_RESOLVE_GLOBAL / MODIFIER_RESOLVE_EXEMPT) shares its body through
// fits a pure compose step; it does not fit here, because the GRID body
// carries a dozen pieces of reused per-frame scratch — hoisting them into a
// shared struct just reinvents `System<N>`.
//
// Cost: a component-less set pays the same cull-gated per-frame re-rasterize
// as an explicit `C_RotationMode{GRID}` set, and the identity arm's
// per-voxel face-occupancy recompute dominates it. Measured on shape_debug
// (macOS/Metal, 300 frames, 8 component-less sets): ~0.19-0.26 ms per UPDATE
// tick for this system, ~0.13 ms/frame at the demo's tick rate, against an
// ~8.5 ms frame. The cull gate bounds it to visible sets; a scene with many
// large static component-less sets pays proportionally more.
template <> struct System<REBUILD_GRID_VOXELS_IMPLICIT> {
    // The mode an absent component means. Passing it explicitly keeps the
    // delegate's GRID gate intact rather than carving a second entry point
    // into it.
    static constexpr C_RotationMode kImplicitGrid{};

    System<REBUILD_GRID_VOXELS> impl_;

    void beginTick() {
        impl_.beginTick();
    }

    void tick(C_VoxelSetNew &voxelSet, const C_WorldTransform &worldTransform) {
        impl_.tick(voxelSet, worldTransform, kImplicitGrid);
    }

    static SystemId create() {
        return registerSystem<
            REBUILD_GRID_VOXELS_IMPLICIT,
            C_VoxelSetNew,
            C_WorldTransform,
            Exclude<C_RotationMode>>("RebuildGridVoxelsImplicit");
    }
};
} // namespace IRSystem

#endif /* SYSTEM_REBUILD_GRID_VOXELS_H */
