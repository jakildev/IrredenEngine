#ifndef COMPONENT_DETACHED_REVOXELIZE_BUFFER_H
#define COMPONENT_DETACHED_REVOXELIZE_BUFFER_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/buffer.hpp>

#include <utility>

using namespace IRRender;

namespace IRComponents {

// Per-pool resident GPU locals buffer for the detached re-voxelize GPU scatter
// (#1556, epic #1553 P2). The resource-model decision behind it (architect, on
// PR #1562): each DETACHED_REVOXELIZE pool owns a resident SSBO of its RIGID
// authored locals so the only per-frame GPU upload is the canvas rotation quat
// (O(entities)), not the O(authored-voxels) re-rasterize P1 paid on the CPU.
//
// `c_revoxelize_detached.{glsl,metal}` binds this buffer (per-canvas, slot
// `kBufferIndex_LocalVoxelPositions`) + the per-frame quat and writes the shared
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
// The home is the architect's option (b) (a per-canvas render component, which
// they noted "also works") rather than a field on C_VoxelPool (their option (a)
// phrasing): C_VoxelPool is a voxel-domain component kept free of
// <irreden/ir_render.hpp> (the T-201 layering boundary, voxel/CLAUDE.md), so the
// GPU-RAII naturally lives on a render-domain sibling on the same canvas entity —
// exactly the C_TriangleCanvasTextures pattern the decision pointed at.
struct C_DetachedRevoxelizeBuffer {
    // Resident SSBO of composed authored locals (local + per-voxel offset),
    // one vec4 per pool slot (.xyz = composed, .w unused). Seeded once, re-seeded
    // only on pool mutation. Drives the IDENTITY fast-path fill (slot == source
    // voxel). {0, nullptr} while unallocated.
    std::pair<ResourceId, Buffer *> residentLocals_{0, nullptr};
    // Voxel count last seeded into residentLocals_. -1 = never seeded; a change
    // (pool mutation) triggers a re-seed. NOT a per-frame dirty flag — the locals
    // are rigid, so this only advances on an actual allocation-size change.
    int seededVoxelCount_ = -1;
    // Buffer capacity in voxels (= pool slot count). The buffer is sized to the
    // pool's full capacity once, so a re-seed never reallocates.
    int capacity_ = 0;

    // Source occupancy+color grid for the INVERSE-resample fill (#1619). Dense
    // 3D grid keyed by integer source-local cell, three uints per cell
    // ({colorPacked, materialFlagBone, reserved}); occupied iff the alpha byte of
    // colorPacked != 0. The reserved lane carries per-trixel priority through a
    // rotating fill (#2023). Seeded with residentLocals_ (rigid). {0, nullptr} while
    // unallocated. The grid is the position→color/occupancy structure the
    // dest-cell inverse lookup needs (forward-scatter's source-indexed locals
    // can't answer "is there a source voxel at p?" in O(1)).
    std::pair<ResourceId, Buffer *> sourceGrid_{0, nullptr};
    IRMath::ivec3 sourceGridMin_{0, 0, 0};  // grid cell (0,0,0) maps to this source cell
    IRMath::ivec3 sourceGridDims_{0, 0, 0}; // per-axis cell count of the grid
    int sourceGridCellCapacity_ = 0;        // allocated grid cells (sized once to high-water)

    // Dest-AABB cube for the inverse resample. The rotated solid is bounded by
    // the origin-centered sphere of radius = farthest authored corner; the dest
    // domain is the enclosing cube [-destCenter_, +destCenter_]³ (rotation-
    // independent, so it never changes per spin pose). destCount_ = destSide_³
    // is the dispatch count + the `voxelCount` the shared compact pass walks.
    int destSide_ = 0;
    int destCenter_ = 0;
    int destCount_ = 0;

    // Per-axis half-cell anchor of the authored solid: composed local minus its
    // roundHalfUp source cell (-0.5 on even-sized centered axes, 0 on odd) —
    // uniform across the pool because the locals are integers and the
    // center-around-origin offset is one shared vector. The inverse resample
    // maps between LATTICE cells while the solid's true points sit at
    // cell + anchor; the anchored mapping keeps the rotated raster from
    // shifting by a constant half cell per even axis (#2349). Seeded with the
    // grids (rigid).
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
        seededVoxelCount_ = -1;
        capacity_ = 0;
        sourceGridMin_ = IRMath::ivec3(0, 0, 0);
        sourceGridDims_ = IRMath::ivec3(0, 0, 0);
        sourceGridCellCapacity_ = 0;
        destSide_ = 0;
        destCenter_ = 0;
        destCount_ = 0;
        anchor_ = IRMath::vec3(0.0f);
    }
};

} // namespace IRComponents

#endif /* COMPONENT_DETACHED_REVOXELIZE_BUFFER_H */
