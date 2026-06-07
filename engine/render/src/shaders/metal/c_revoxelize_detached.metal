#include <metal_stdlib>
using namespace metal;

// Detached re-voxelize GPU scatter fill (#1556, epic #1553 P2). Mirrors
// shaders/c_revoxelize_detached.glsl byte-for-byte: one thread per live voxel of
// a DETACHED_REVOXELIZE pool rotates the voxel's RIGID authored local position by
// the canvas rotation quat and writes the resulting integer CELL into the shared
// global-position buffer (buffer 5) consumed by VOXEL_TO_TRIXEL_STAGE_1. The
// rotation lives in the cell positions; the canvas rasterizes its pool through
// CARDINAL/static frame data (no 2D forward-scatter deform).
//
// `rotateByQuat` + `roundHalfUp` are the shared CPU↔GPU helpers in
// ir_iso_common.metal (CPU mirror IRMath::rotateVectorByQuat /
// IRMath::roundVec3HalfUp), kept bit-identical with the GLSL + CPU so half-
// integers classify the same on every backend. The threadgroup grid + linear
// index reconstruction match c_update_voxel_positions.metal exactly.

#include "ir_iso_common.metal"

struct RevoxelizeParams {
    float4 canvasRotation_; // (qx, qy, qz, qw); identity = (0,0,0,1)
    int voxelCount_;
};

kernel void c_revoxelize_detached(
    device float4* globalPositions [[buffer(5)]],
    device const float4* residentLocals [[buffer(17)]],
    constant RevoxelizeParams& params [[buffer(16)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 groupCount [[threadgroups_per_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    const uint workGroupIndex = groupId.x + groupId.y * groupCount.x;
    const uint voxelId = workGroupIndex * 64u + localId.x;
    if (voxelId >= uint(params.voxelCount_)) {
        return;
    }

    const float3 composed = residentLocals[voxelId].xyz;
    float3 cell;
    if (all(params.canvasRotation_ == float4(0.0, 0.0, 0.0, 1.0))) {
        // Identity fast-path: worldCellForGridVoxel returns the composed local
        // unrounded (no scale, no translation).
        cell = composed;
    } else {
        cell = float3(roundHalfUp(rotateByQuat(composed, params.canvasRotation_)));
    }
    // Match the CPU mirror's `.w` (VoxelGpuPosition::pad_ defaults to 0 for a
    // static-transform re-voxelize voxel; buffer 5's .w is not read by the
    // raster, which keys on .xyz).
    globalPositions[voxelId] = float4(cell, 0.0);
}
