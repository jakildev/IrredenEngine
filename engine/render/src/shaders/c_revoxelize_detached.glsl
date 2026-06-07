#version 450 core

// Detached re-voxelize GPU scatter fill (#1556, epic #1553 P2). The GPU analogue
// of the P1 CPU REBUILD_DETACHED_VOXELS: one thread per live voxel of a
// DETACHED_REVOXELIZE pool rotates the voxel's RIGID authored local position by
// the canvas rotation quat and writes the resulting integer CELL into the shared
// global-position SSBO (binding 5) that VOXEL_TO_TRIXEL_STAGE_1 reads. The
// rotation lives in the cell positions; the canvas then rasterizes its pool
// through CARDINAL/static frame data (no 2D forward-scatter deform).
//
// The per-pool resident locals (binding 17) are seeded once at allocation and
// re-seeded only on pool mutation, so the only per-frame upload is the canvas
// quat in RevoxelizeParams — O(entities), not O(authored voxels).
//
// BIT-IDENTICAL to the CPU IRPrefab::GridRotation::worldCellForGridVoxel under
// {translation = 0, scale = 1}: identity rotation passes the composed local
// through unrounded; any other rotation rotates then `roundHalfUp`s each axis.
// `rotateByQuat` + `roundHalfUp` are the shared CPU↔GPU helpers in
// ir_iso_common.glsl (CPU mirrors IRMath::rotateVectorByQuat /
// IRMath::roundVec3HalfUp), so CPU, GLSL and Metal classify half-integers the
// same. The 2D workgroup grid + linear index reconstruction mirror
// c_update_voxel_positions.glsl exactly (same dispatch helper on the CPU).

#include "ir_iso_common.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// binding 5 — shared "VoxelPositionBuffer", consumed by VOXEL_TO_TRIXEL_STAGE_1.
layout(std430, binding = 5) buffer GlobalPositionBuffer {
    vec4 globalPositions[];
};

// binding 17 — this pool's resident authored locals (.xyz = local + per-voxel
// offset, already composed; .w unused). Rigid: seeded once, never per frame.
layout(std430, binding = 17) readonly buffer ResidentLocalsBuffer {
    vec4 residentLocals[];
};

// binding 16 — per-frame params: the canvas rotation quat + live voxel count.
layout(std140, binding = 16) uniform RevoxelizeParams {
    vec4 canvasRotation_; // (qx, qy, qz, qw); identity = (0,0,0,1)
    int voxelCount_;
};

void main() {
    uint workGroupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    uint voxelId = workGroupIndex * 64u + gl_LocalInvocationID.x;
    if (voxelId >= uint(voxelCount_)) {
        return;
    }

    vec3 composed = residentLocals[voxelId].xyz;
    vec3 cell;
    if (canvasRotation_ == vec4(0.0, 0.0, 0.0, 1.0)) {
        // Identity fast-path: worldCellForGridVoxel returns the composed local
        // unrounded (no scale, no translation), so the cardinal cells are
        // untouched.
        cell = composed;
    } else {
        cell = vec3(roundHalfUp(rotateByQuat(composed, canvasRotation_)));
    }
    // Match the CPU mirror's `.w` (VoxelGpuPosition::pad_ defaults to 0 for a
    // static-transform re-voxelize voxel; binding 5's .w is not read by the
    // raster, which keys on .xyz).
    globalPositions[voxelId] = vec4(cell, 0.0);
}
