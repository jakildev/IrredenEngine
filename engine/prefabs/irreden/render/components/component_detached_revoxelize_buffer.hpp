#ifndef COMPONENT_DETACHED_REVOXELIZE_BUFFER_H
#define COMPONENT_DETACHED_REVOXELIZE_BUFFER_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/buffer.hpp>

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

using namespace IRRender;

namespace IRComponents {

// Rigid, rotation-independent seed of one cell group of a detached re-voxelize
// pool: where its source occupancy grid sits in the pool's concatenated grid
// buffer, and the dest-slot range its resample fills.
struct RevoxelizeGroupSeed {
    // Pool span the group was seeded from.
    std::size_t spanStart_ = 0;
    std::size_t spanCount_ = 0;
    // Source occupancy+color grid: cell (0,0,0) of the grid is `gridMin_`.
    IRMath::ivec3 gridMin_{0, 0, 0};
    IRMath::ivec3 gridDims_{0, 0, 0};
    // First word of this group's grid in `sourceGrid_`.
    int gridWordBase_ = 0;
    // Per-axis half-cell anchor of the authored solid: composed local minus its
    // roundHalfUp source cell (-0.5 on even-sized centered axes, 0 on odd) —
    // uniform across the group because the locals are integers and the
    // center-around-origin offset is one shared vector.
    IRMath::vec3 anchor_{0.0f, 0.0f, 0.0f};
    // Dest cube: the group under any rotation fits the cube of `destSide_ =
    // 2·destCenter_ + 1` cells per axis around its translation.
    int destCenter_ = 0;
    int destSide_ = 0;
    // First dest slot of the group; groups are laid out contiguously in span
    // order.
    int destSlotBase_ = 0;
};

// Per-pool resident GPU locals buffer for the detached re-voxelize GPU scatter.
// Each DETACHED_REVOXELIZE pool owns a resident SSBO of its RIGID
// authored locals so the only per-frame GPU upload is the pose of each hosted
// set (O(cell groups)), not O(authored voxels).
//
// `c_revoxelize_detached.{glsl,metal}` binds this buffer (per-canvas, slot
// `kBufferIndex_LocalVoxelPositions`) + the per-frame poses and writes the shared
// global-position SSBO (binding 5) for that pool, dispatched from
// VOXEL_TO_TRIXEL_STAGE_1's per-canvas tick in place of `flushStaticPositionRanges`.
//
// GPU-resource-RAII component (engine/prefabs/CLAUDE.md §"Documented exceptions"):
// the component owns the buffer and frees it in onDestroy(). Like
// C_PerAxisTrixelCanvases it allocates LAZILY (not in the ctor) — bundled inert
// on every voxel-pool canvas, stood up only for a re-voxelize canvas by
// IRPrefab::DetachedRevoxelize::syncResidentBuffers(). A static / non-re-voxelize
// canvas pays only the component slot, no GPU memory.
//
// This is a per-canvas render component rather than a C_VoxelPool field because
// C_VoxelPool is a voxel-domain component kept free of
// <irreden/ir_render.hpp> (the layering boundary, voxel_pool_api.hpp), so the
// GPU-RAII lives on a render-domain sibling on the same canvas entity.
struct C_DetachedRevoxelizeBuffer {
    // Resident SSBO of composed authored locals (local + per-voxel offset),
    // one vec4 per pool slot (.xyz = composed, .w unused). Seeded once, re-seeded
    // only on pool mutation. Drives the IDENTITY fast-path fill (slot == source
    // voxel). {0, nullptr} while unallocated.
    std::pair<ResourceId, Buffer *> residentLocals_{0, nullptr};
    // Pool content generation (C_VoxelPool::getContentGeneration) last seeded
    // from. 0 = never seeded: a pool with live voxels has allocated at least
    // once. A change (span allocated or freed, voxel records rewritten) triggers
    // a re-seed. NOT a per-frame dirty flag — the pool advances it at mutation
    // time, so a steady pool never re-seeds.
    std::uint64_t seededContentGeneration_ = 0;
    // Buffer capacity in voxels (= pool slot count). The buffer is sized to the
    // pool's full capacity once, so a re-seed never reallocates.
    int capacity_ = 0;

    // Source occupancy+color grids for the INVERSE-resample fill, one per cell
    // group, concatenated. Each is a dense 3D grid keyed by integer
    // source-local cell, three uints per cell ({colorPacked, materialFlagBone,
    // reserved}); occupied iff the alpha byte of colorPacked != 0. The reserved
    // lane carries per-trixel priority through a rotating fill. Seeded with
    // residentLocals_ (rigid). {0, nullptr} while unallocated. The grid is the
    // position→color/occupancy structure the dest-cell inverse lookup needs
    // (forward-scatter's source-indexed locals can't answer "is there a source
    // voxel at p?" in O(1)).
    std::pair<ResourceId, Buffer *> sourceGrid_{0, nullptr};
    int sourceGridCellCapacity_ = 0; // allocated grid cells (sized once to high-water)

    // One seed per cell group, in span order — the same order the pool lists
    // its groups in. A pool with no posted groups seeds exactly one implicit
    // group spanning its live prefix. A re-seed is gated on this span set and
    // on `seededContentGeneration_`.
    std::vector<RevoxelizeGroupSeed> groups_;

    // Total dest slots across the groups: the dispatch count and the
    // `voxelCount` the shared compact pass walks. Rotation-independent.
    int destCount_ = 0;

    // The canvas lattice phase: a dest cell rasters at `cell + anchor_`. It is
    // the first group's half-cell anchor, so a single-set canvas keeps its
    // solid centred on the canvas origin; every other group resamples onto the
    // same lattice through its own translation.
    IRMath::vec3 anchor_{0.0f, 0.0f, 0.0f};

    // Allocation state is the handle itself — no separate bool to drift
    // (.claude/rules/cpp-ecs.md "No dirty flags"). Both buffers are allocated
    // together, so the resident-locals handle gates them both.
    bool isAllocated() const {
        return residentLocals_.second != nullptr;
    }

    void onDestroy() {
        if (residentLocals_.second != nullptr) {
            IRRender::destroyResource<Buffer>(residentLocals_.first);
            residentLocals_ = {0, nullptr};
        }
        if (sourceGrid_.second != nullptr) {
            IRRender::destroyResource<Buffer>(sourceGrid_.first);
            sourceGrid_ = {0, nullptr};
        }
        seededContentGeneration_ = 0;
        capacity_ = 0;
        sourceGridCellCapacity_ = 0;
        groups_.clear();
        destCount_ = 0;
        anchor_ = IRMath::vec3(0.0f);
    }
};

} // namespace IRComponents

#endif /* COMPONENT_DETACHED_REVOXELIZE_BUFFER_H */
