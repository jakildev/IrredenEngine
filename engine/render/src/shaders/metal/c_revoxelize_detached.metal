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
//    inverse-maps to source cell `roundHalfUp(R⁻¹·c)`; if occupied (per-pool source
//    grid, buffer 9) the thread authors position (5) + color (6) + the active bit
//    (8, atomic). Surjective → hole-free. The shared compact → stage1 → stage2
//    raster is untouched; slot `i` now means "dest cell i" not "source voxel i".
//
// `rotateByQuat` / `rotateByInverseQuat` / `roundHalfUp` are the shared CPU↔GPU
// helpers in ir_iso_common.metal, bit-identical with GLSL + CPU. The exposed-face
// mask is NOT authored here: re-voxelize frame data marks `.w = 1` so stage 1/2
// emit all three cardinal faces and depth-resolve the front surface.

#include "ir_iso_common.metal"

struct RevoxelizeParams {
    float4 canvasRotation_; // (qx, qy, qz, qw); identity = (0,0,0,1)
    int4 dest_;             // x = dispatch count, y = dest side, z = dest center, w = inverse mode
    int4 srcGridMin_;       // xyz = source grid min cell
    int4 srcGridDims_;      // xyz = source grid dims
};

struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};

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

    // MODE 1 — inverse resample. Decode dest cell from the linear slot.
    const int side = params.dest_.y;
    const int center = params.dest_.z;
    const int3 d = int3(
        int(slot) % side,
        (int(slot) / side) % side,
        int(slot) / (side * side)
    );
    const int3 destCell = d - int3(center);

    const int3 src = roundHalfUp(rotateByInverseQuat(float3(destCell), params.canvasRotation_));
    const int3 g = src - params.srcGridMin_.xyz;
    uint colorPacked = 0u;
    uint matFlagBone = 0u;
    if (all(g >= int3(0)) && all(g < params.srcGridDims_.xyz)) {
        const int li = g.x + params.srcGridDims_.x * (g.y + params.srcGridDims_.y * g.z);
        colorPacked = sourceGrid[2 * li];
        matFlagBone = sourceGrid[2 * li + 1];
    }

    if (((colorPacked >> 24u) & 0xFFu) != 0u) {
        globalPositions[slot] = float4(float3(destCell), 0.0);
        Voxel v;
        v.colorPacked = colorPacked;
        v.materialFlagBone = matFlagBone;
        v.reserved = 0u;
        destColors[slot] = v;
        atomic_fetch_or_explicit(
            &activeMask[slot >> 5u],
            1u << (slot & 31u),
            memory_order_relaxed
        );
    }
    // Empty dest cells: active bit stays 0 (CPU pre-cleared); slots not read.
}
