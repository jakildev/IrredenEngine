#include <metal_stdlib>
using namespace metal;

// Detached re-voxelize GPU fill (#1556 P2, inverse-resample coverage fix #1619).
// Mirrors shaders/c_revoxelize_detached.glsl byte-for-byte. Two dispatch modes
// (RevoxelizeParams.dest_.w):
//
//  MODE 0 — IDENTITY / source path (byte-identical to pre-#1619). One thread per
//    LIVE SOURCE voxel; rotates+rounds its resident composed local into buffer 5.
//    The CPU uploads color + active for these source-indexed slots.
//
//  MODE 1 — INVERSE RESAMPLE (#1619). One thread per DEST cell of the rotated-
//    AABB cube. Forward scatter is not surjective onto the rotated lattice (holes);
//    inverse resampling dispatches over the DEST lattice and pulls: dest cell `c`
//    inverse-maps to source cell `roundHalfUp(R⁻¹·(c + anchor) - anchor)` (the
//    half-cell-anchored map, #2349); if occupied (per-pool source
//    grid, buffer 9) the thread authors position (5) + color (6) + the active bit
//    (8, atomic). Surjective → hole-free. The shared compact → stage1 → stage2
//    raster is untouched; slot `i` now means "dest cell i" not "source voxel i".
//
// `rotateByQuat` / `rotateByInverseQuat` / `roundHalfUp` are the shared CPU↔GPU
// helpers in ir_iso_common.metal, bit-identical with GLSL + CPU. MODE 1 also
// authors the ROTATED-frame face-occlusion mask from dest-grid adjacency (the
// GPU twin of REBUILD_GRID_VOXELS' #1720 CPU mask), so stage 1/2 gate the
// re-voxelize emit on faceIsExposed like the GRID path — see the GLSL twin.

#include "ir_iso_common.metal"

struct RevoxelizeParams {
    float4 canvasRotation_; // (qx, qy, qz, qw); identity = (0,0,0,1)
    int4 dest_;             // x = dispatch count, y = dest side, z = dest center, w = inverse mode
    int4 srcGridMin_;       // xyz = source grid min cell
    int4 srcGridDims_;      // xyz = source grid dims
    float4 anchor_;         // xyz = half-cell anchor: solid point = cell + anchor (#2349)
};

struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};

// The solid's true points sit at `cell + anchor` (see the GLSL twin, #2349):
// source cell for the anchored dest point = roundHalfUp(R⁻¹·(c + a) - a).
static inline int3 revoxSourceCellForDest(int3 destCell, float4 rot, float3 anchor) {
    const float3 destPoint = float3(destCell) + anchor;
    return roundHalfUp(rotateByInverseQuat(destPoint, rot) - anchor);
}

// Is dest cell `c` covered? Inverse-map to source + check occupancy — the GPU
// twin of REBUILD_GRID_VOXELS' #1720 dest-grid adjacency probe.
static inline bool revoxDestCovered(
    int3 c, float4 rot, float3 anchor, int3 srcGridMin, int3 srcGridDims,
    device const uint* sourceGrid
) {
    const int3 src = revoxSourceCellForDest(c, rot, anchor);
    const int3 g = src - srcGridMin;
    if (any(g < int3(0)) || any(g >= srcGridDims)) {
        return false;
    }
    const int li = g.x + srcGridDims.x * (g.y + srcGridDims.y * g.z);
    return ((sourceGrid[3 * li] >> 24u) & 0xFFu) != 0u;
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
    const uint slot = workGroupIndex * 64u + localId.x;
    if (slot >= uint(params.dest_.x)) {
        return;
    }

    if (params.dest_.w == 0) {
        // MODE 0 — identity / source path. Slot == source voxel; identity passes
        // the composed local through unrounded (rotate branch kept for parity).
        const float3 composed = residentLocals[slot].xyz;
        float3 cell;
        if (all(params.canvasRotation_ == float4(0.0, 0.0, 0.0, 1.0))) {
            cell = composed;
        } else {
            cell = float3(roundHalfUp(rotateByQuat(composed, params.canvasRotation_)));
        }
        globalPositions[slot] = float4(cell, 0.0);
        return;
    }

    // MODE 1 — inverse resample. Decode dest cell from the linear slot,
    // recentered — shifted +1 on anchored axes: with anchor = -0.5 the dest
    // cells (roundHalfUp(p - anchor), p in [-r, r]) span the SAME cell count
    // one cell higher, so shifting the decode window covers them at zero
    // dispatch growth (#2349). Mirrors revoxDestDecodeShift in the GLSL twin.
    const int side = params.dest_.y;
    const int center = params.dest_.z;
    const float4 rot = params.canvasRotation_;
    const float3 anc = params.anchor_.xyz;
    const int3 d = int3(
        int(slot) % side,
        (int(slot) / side) % side,
        int(slot) / (side * side)
    );
    const int3 revoxDestDecodeShift = select(int3(0), int3(1), anc < float3(-0.25));
    const int3 destCell = d - int3(center) + revoxDestDecodeShift;

    const int3 src = revoxSourceCellForDest(destCell, rot, anc);
    const int3 g = src - params.srcGridMin_.xyz;
    uint colorPacked = 0u;
    uint matFlagBone = 0u;
    uint reserved = 0u;
    if (all(g >= int3(0)) && all(g < params.srcGridDims_.xyz)) {
        const int li = g.x + params.srcGridDims_.x * (g.y + params.srcGridDims_.y * g.z);
        colorPacked = sourceGrid[3 * li];
        matFlagBone = sourceGrid[3 * li + 1];
        reserved = sourceGrid[3 * li + 2];
    }

    if (((colorPacked >> 24u) & 0xFFu) != 0u) {
        // Anchored raster position (cell + anchor), matching mode 0's unrounded
        // composed locals at identity (#2349).
        globalPositions[slot] = float4(float3(destCell) + params.anchor_.xyz, 0.0);
        // Author the ROTATED-frame face-occlusion mask from dest-grid adjacency
        // (GPU twin of REBUILD_GRID_VOXELS #1720), replacing the stale unrotated
        // source mask, so stage 1/2 gate the re-voxelize emit on faceIsExposed
        // rather than bypassing it. The bypass emitted all three cardinal faces
        // and let a slot-tie checkerboard winner drive AO hatching on flat
        // surfaces — the divergence from the GRID path, which DOES author this
        // mask. occ uses the kFaceOccluded* bit layout (component_voxel.hpp): a
        // neighbour-occupied face is occluded; flagsByte (bits 2..7) sits at
        // matFlagBone bits 10..15.
        const int3 gmin = params.srcGridMin_.xyz;
        const int3 gdim = params.srcGridDims_.xyz;
        uint occ = 0u;
        if (revoxDestCovered(destCell + int3(-1, 0, 0), rot, anc, gmin, gdim, sourceGrid)) occ |= (1u << 2);
        if (revoxDestCovered(destCell + int3( 1, 0, 0), rot, anc, gmin, gdim, sourceGrid)) occ |= (1u << 3);
        if (revoxDestCovered(destCell + int3(0, -1, 0), rot, anc, gmin, gdim, sourceGrid)) occ |= (1u << 4);
        if (revoxDestCovered(destCell + int3(0,  1, 0), rot, anc, gmin, gdim, sourceGrid)) occ |= (1u << 5);
        if (revoxDestCovered(destCell + int3(0, 0, -1), rot, anc, gmin, gdim, sourceGrid)) occ |= (1u << 6);
        if (revoxDestCovered(destCell + int3(0, 0,  1), rot, anc, gmin, gdim, sourceGrid)) occ |= (1u << 7);
        matFlagBone = (matFlagBone & ~(0x3Fu << 10)) | (occ << 8);
        // Carry the source voxel's reserved word (per-trixel priority in
        // bits[1:0], #1960 / #2023) into the dest record verbatim — the same
        // word the static buffer-6 upload writes; stage 2 masks `& 0x3u` at
        // decode. Without this the rotating fill hardcoded reserved 0, so a
        // spinning detached solid silently lost its per-trixel depth priority.
        Voxel v;
        v.colorPacked = colorPacked;
        v.materialFlagBone = matFlagBone;
        v.reserved = reserved;
        destColors[slot] = v;
        atomic_fetch_or_explicit(
            &activeMask[slot >> 5u],
            1u << (slot & 31u),
            memory_order_relaxed
        );
    }
    // Empty dest cells: active bit stays 0 (CPU pre-cleared); slots not read.
}
