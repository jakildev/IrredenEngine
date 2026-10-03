#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_revoxelize_detached.glsl byte-for-byte. Two dispatch modes
// (RevoxelizeParams.dest_.w). MODE 0 authors only the position buffer (5); the
// CPU uploads color + active for those source-indexed slots. MODE 1 resamples
// over the DEST lattice because a forward scatter is not surjective onto the
// rotated lattice (holes); the half-cell-anchored inverse map
// `roundHalfUp(R_g⁻¹·(c + destOffset_g) - anchor_g)` is, so the fill is
// hole-free. A pool hosts one or more rigid voxel sets, each a CELL GROUP with
// its own pose, source grid and contiguous range of dest slots; a thread finds
// its group from its slot. In MODE 1 the kernel also authors color (6) and the
// active bit (8, atomic), and slot `i` means "dest cell i", not "source voxel
// i", to the shared compact → stage1 → stage2 raster.
//
// `rotateByQuat` / `rotateByInverseQuat` / `roundHalfUp` are the shared CPU↔GPU
// helpers in ir_iso_common.metal, bit-identical with GLSL + CPU. MODE 1 also
// authors the ROTATED-frame face-occlusion mask from dest-grid adjacency within
// the group (the GPU twin of REBUILD_GRID_VOXELS' CPU mask), so stage 1/2 gate
// the re-voxelize emit on faceIsExposed like the GRID path.

#include "ir_iso_common.metal"

// Mirrors IRRender::kRevoxelizeGroupsPerDispatch.
#define REVOXELIZE_GROUPS_PER_DISPATCH 32

struct RevoxelizeGroup {
    float4 rotation;   // group rotation in the canvas frame, (qx, qy, qz, qw)
    float4 destOffset; // xyz: group-centered point = dest cell + destOffset
    float4 anchor;     // xyz = half-cell anchor: solid point = cell + anchor
    int4 destBase;     // xyz = dest cell of the group's first slot, w = dest side
    int4 srcGridMin;   // xyz = source grid min cell, w = the group's first dest slot
    int4 srcGridDims;  // xyz = source grid dims, w = the grid's first word
};

struct RevoxelizeParams {
    float4 canvasRotation_; // mode 0: (qx, qy, qz, qw); identity = (0,0,0,1)
    int4 dest_;             // x = thread count, y = group count, z = first dest slot, w = inverse mode
    float4 phase_;          // xyz = canvas lattice phase: raster position = cell + phase
    RevoxelizeGroup groups_[REVOXELIZE_GROUPS_PER_DISPATCH];
};

struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};

// The solid's true points sit at `cell + anchor` (-0.5 on even-sized centered
// axes, 0 on odd), so the inverse resample rotates the anchored POINTS, not the
// raw lattice cells: source cell for the group-centered point p =
// roundHalfUp(R⁻¹·p - anchor).
static inline int3 revoxSourceCellForDest(int3 destCell, thread const RevoxelizeGroup& group) {
    const float3 destPoint = float3(destCell) + group.destOffset.xyz;
    return roundHalfUp(rotateByInverseQuat(destPoint, group.rotation) - group.anchor.xyz);
}

// Word index of source cell `src` in the group's grid, or -1 outside it.
static inline int revoxSourceWord(int3 src, thread const RevoxelizeGroup& group) {
    const int3 g = src - group.srcGridMin.xyz;
    const int3 dims = group.srcGridDims.xyz;
    if (any(g < int3(0)) || any(g >= dims)) {
        return -1;
    }
    return group.srcGridDims.w + 3 * (g.x + dims.x * (g.y + dims.y * g.z));
}

// Is dest cell `c` covered by the group? Inverse-map to source + check
// occupancy — the GPU twin of REBUILD_GRID_VOXELS' dest-grid adjacency probe.
static inline bool revoxDestCovered(
    int3 c, thread const RevoxelizeGroup& group, device const uint* sourceGrid
) {
    const int word = revoxSourceWord(revoxSourceCellForDest(c, group), group);
    if (word < 0) {
        return false;
    }
    return ((sourceGrid[word] >> 24u) & 0xFFu) != 0u;
}

kernel void c_revoxelize_detached(
    device float4* globalPositions [[buffer(5)]],
    device Voxel* destColors [[buffer(6)]],
    device atomic_uint* activeMask [[buffer(8)]],
    device const uint* sourceGrid [[buffer(9)]],
    device const float4* residentLocals [[buffer(17)]],
    constant RevoxelizeParams& params [[buffer(16)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 groupCount [[threadgroups_per_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    const uint workGroupIndex = groupId.x + groupId.y * groupCount.x;
    const uint threadIndex = workGroupIndex * 64u + localId.x;
    if (threadIndex >= uint(params.dest_.x)) {
        return;
    }

    if (params.dest_.w == 0) {
        // MODE 0 — identity / source path. Slot == source voxel. Identity passes
        // the composed local through unrounded: it can sit at a half-integer
        // anchor, which roundHalfUp would shift.
        const float3 composed = residentLocals[threadIndex].xyz;
        float3 cell;
        if (all(params.canvasRotation_ == float4(0.0, 0.0, 0.0, 1.0))) {
            cell = composed;
        } else {
            cell = float3(roundHalfUp(rotateByQuat(composed, params.canvasRotation_)));
        }
        globalPositions[threadIndex] = float4(cell, 0.0);
        return;
    }

    // MODE 1 — inverse resample. The groups' dest-slot ranges are contiguous
    // and ascending, so the owning group is the last one starting at or before
    // this slot.
    const int slot = params.dest_.z + int(threadIndex);
    int groupIndex = 0;
    for (int i = 1; i < params.dest_.y; ++i) {
        if (slot >= params.groups_[i].srcGridMin.w) {
            groupIndex = i;
        }
    }
    const RevoxelizeGroup group = params.groups_[groupIndex];

    // Decode this thread's dest cell from its position in the group's [side]³
    // cube. `destBase` is the CPU-resolved first cell of the smallest window
    // that holds the group under any rotation at its current translation.
    const int side = group.destBase.w;
    const int slotInGroup = slot - group.srcGridMin.w;
    const int3 d = int3(
        slotInGroup % side,
        (slotInGroup / side) % side,
        slotInGroup / (side * side)
    );
    const int3 destCell = d + group.destBase.xyz;

    const int word = revoxSourceWord(revoxSourceCellForDest(destCell, group), group);
    uint colorPacked = 0u;
    uint matFlagBone = 0u;
    uint reserved = 0u;
    if (word >= 0) {
        colorPacked = sourceGrid[word];
        matFlagBone = sourceGrid[word + 1];
        reserved = sourceGrid[word + 2];
    }

    if (((colorPacked >> 24u) & 0xFFu) != 0u) {
        // Lattice raster position (cell + phase), matching mode 0's unrounded
        // composed locals at identity.
        globalPositions[slot] = float4(float3(destCell) + params.phase_.xyz, 0.0);
        // Author the ROTATED-frame face-occlusion mask from dest-grid adjacency
        // (GPU twin of REBUILD_GRID_VOXELS), replacing the unrotated source mask,
        // so stage 1/2 gate the re-voxelize emit on faceIsExposed. occ uses the
        // kFaceOccluded* bit layout (component_voxel.hpp): a neighbour-occupied
        // face is occluded; flagsByte (bits 2..7) sits at matFlagBone bits 10..15.
        uint occ = 0u;
        if (revoxDestCovered(destCell + int3(-1, 0, 0), group, sourceGrid)) occ |= (1u << 2);
        if (revoxDestCovered(destCell + int3( 1, 0, 0), group, sourceGrid)) occ |= (1u << 3);
        if (revoxDestCovered(destCell + int3(0, -1, 0), group, sourceGrid)) occ |= (1u << 4);
        if (revoxDestCovered(destCell + int3(0,  1, 0), group, sourceGrid)) occ |= (1u << 5);
        if (revoxDestCovered(destCell + int3(0, 0, -1), group, sourceGrid)) occ |= (1u << 6);
        if (revoxDestCovered(destCell + int3(0, 0,  1), group, sourceGrid)) occ |= (1u << 7);
        matFlagBone = (matFlagBone & ~(0x3Fu << 10)) | (occ << 8);
        // Carry the source voxel's reserved word (per-trixel priority in
        // bits[1:0]) into the dest record verbatim — the same word the static
        // buffer-6 upload writes; stage 2 masks `& 0x3u` at decode.
        Voxel v;
        v.colorPacked = colorPacked;
        v.materialFlagBone = matFlagBone;
        v.reserved = reserved;
        destColors[slot] = v;
        atomic_fetch_or_explicit(
            &activeMask[uint(slot) >> 5u],
            1u << (uint(slot) & 31u),
            memory_order_relaxed
        );
    }
    // Empty dest cells: active bit stays 0 (CPU pre-cleared); slots not read.
}
