#version 450 core

// Detached re-voxelize GPU fill (#1556 P2, inverse-resample coverage fix #1619).
// Fills the shared global-position SSBO (binding 5) — and, in inverse mode, the
// color (6) + active-mask (8) SSBOs — for a DETACHED_REVOXELIZE pool so the
// canvas rasterizes its rotated solid through CARDINAL/static frame data (no 2D
// forward-scatter deform). Two dispatch modes (RevoxelizeParams.dest_.w):
//
//  MODE 0 — IDENTITY / source path (byte-identical to pre-#1619). One thread per
//    LIVE SOURCE voxel; the thread rotates+rounds its resident composed local and
//    writes the cell to binding 5. The CPU uploads color + active for these
//    source-indexed slots, so this mode authors only binding 5. The CPU only
//    selects mode 0 when the canvas rotation is identity, so the rotate branch is
//    a passthrough here (kept verbatim for byte-identity).
//
//  MODE 1 — INVERSE RESAMPLE (#1619). One thread per DEST cell of the rotated-
//    AABB cube (`dest_.y³` cells, center `dest_.z`). Forward scatter (mode 0 under
//    rotation) is NOT surjective onto the rotated lattice → covered dest cells got
//    no source voxel → holes. Inverse resampling dispatches over the DEST lattice
//    and pulls: dest cell `c` inverse-maps to source cell
//    `roundHalfUp(R⁻¹·(c + anchor) - anchor)` (the half-cell-anchored map, #2349); if
//    that source cell is occupied (in the per-pool source grid, binding 9) the
//    thread authors `position[slot]=c`, `color[slot]=srcColor`, and sets the
//    per-slot active bit. Surjective by construction → hole-free at every size.
//    The shared compact → stage1 → stage2 raster is UNTOUCHED: it still reads
//    position[i]/color[i]/active[i] and rasterizes active slots — slot `i` now
//    means "dest cell i" instead of "source voxel i".
//
// `rotateByQuat` / `rotateByInverseQuat` / `roundHalfUp` are the shared CPU↔GPU
// helpers in ir_iso_common.glsl (CPU mirrors IRMath::rotateVectorByQuat /
// IRMath::roundVec3HalfUp), so CPU, GLSL and Metal classify half-integers the
// same. MODE 1 ALSO authors the ROTATED-frame face-occlusion mask from dest-grid
// adjacency (the GPU twin of REBUILD_GRID_VOXELS' #1720 CPU mask), so stage 1/2
// gate the re-voxelize emit on `faceIsExposed` exactly like the GRID path —
// instead of the old `.w = 1` bypass that emitted all three cardinal faces and
// let a slot-tie checkerboard winner drive AO hatching on flat surfaces. `.w`
// still marks re-voxelize (for the emit dilation), only the gate changed.

#include "ir_iso_common.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// binding 5 — shared "VoxelPositionBuffer", consumed by VOXEL_TO_TRIXEL_STAGE_1.
layout(std430, binding = 5) buffer GlobalPositionBuffer {
    vec4 globalPositions[];
};

// binding 6 — shared color SSBO. Inverse mode authors the dest-cell color here
// (the CPU color upload is skipped for that path). Same 3-uint Voxel record the
// raster reads (component_voxel.hpp / c_voxel_to_trixel_stage_1.glsl).
struct Voxel {
    uint colorPacked;
    uint materialFlagBone;
    uint reserved;
};
layout(std430, binding = 6) buffer VoxelColorBuffer {
    Voxel destColors[];
};

// binding 8 — shared per-slot active bitmask consumed by the compact pass.
// Inverse mode sets the bit for each occupied dest slot (CPU pre-clears the
// window to 0); empty dest cells stay inactive and are skipped by the compact.
layout(std430, binding = 8) buffer VoxelActiveMaskBuffer {
    uint activeMask[];
};

// binding 9 — this pool's source occupancy+color grid (inverse lookup). Three
// uints per source cell: [3k] = colorPacked (occupied iff alpha byte != 0),
// [3k+1] = materialFlagBone, [3k+2] = reserved (per-trixel priority carrier,
// #2023). Keyed by `srcCell - srcGridMin_`.
layout(std430, binding = 9) readonly buffer RevoxelizeSourceGrid {
    uint sourceGrid[];
};

// binding 17 — this pool's resident authored locals (.xyz = composed local).
// Read only by the identity / source path (mode 0).
layout(std430, binding = 17) readonly buffer ResidentLocalsBuffer {
    vec4 residentLocals[];
};

// binding 16 — per-frame params: canvas quat + dispatch-domain descriptor.
layout(std140, binding = 16) uniform RevoxelizeParams {
    vec4 canvasRotation_; // (qx, qy, qz, qw); identity = (0,0,0,1)
    ivec4 dest_;          // x = dispatch count, y = dest side, z = dest center, w = inverse mode
    ivec4 srcGridMin_;    // xyz = source grid min cell
    ivec4 srcGridDims_;   // xyz = source grid dims
    vec4 anchor_;         // xyz = half-cell anchor: solid point = cell + anchor (#2349)
};

// The solid's true points sit at `cell + anchor_` (an even-sized centered axis
// authors at half-integers; roundHalfUp shifted its source cells by +0.5, so
// anchor is -0.5 there, 0 on odd axes). The inverse resample must rotate the
// anchored POINTS, not the raw lattice cells — mapping cells directly shifted
// the whole rotated raster by a constant half cell per even axis (#2349).
// Source cell for the anchored point p: roundHalfUp(p - anchor_).
ivec3 revoxSourceCellForDest(ivec3 destCell) {
    vec3 destPoint = vec3(destCell) + anchor_.xyz;
    return roundHalfUp(rotateByInverseQuat(destPoint, canvasRotation_) - anchor_.xyz);
}

// Is dest cell `c` covered? Inverse-map to source + check occupancy — the GPU
// twin of REBUILD_GRID_VOXELS' #1720 dest-grid adjacency probe.
bool revoxDestCovered(ivec3 c) {
    ivec3 src = revoxSourceCellForDest(c);
    ivec3 g = src - srcGridMin_.xyz;
    if (any(lessThan(g, ivec3(0))) || any(greaterThanEqual(g, srcGridDims_.xyz))) {
        return false;
    }
    int li = g.x + srcGridDims_.x * (g.y + srcGridDims_.y * g.z);
    return ((sourceGrid[3 * li] >> 24u) & 0xFFu) != 0u;
}

void main() {
    uint workGroupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    uint slot = workGroupIndex * 64u + gl_LocalInvocationID.x;
    if (slot >= uint(dest_.x)) {
        return;
    }

    if (dest_.w == 0) {
        // MODE 0 — identity / source path. Slot == source voxel. Identity passes
        // the integer composed local through unrounded (roundHalfUp would be a
        // no-op); the rotate branch is kept for byte-identity but never selected.
        vec3 composed = residentLocals[slot].xyz;
        vec3 cell;
        if (canvasRotation_ == vec4(0.0, 0.0, 0.0, 1.0)) {
            cell = composed;
        } else {
            cell = vec3(roundHalfUp(rotateByQuat(composed, canvasRotation_)));
        }
        globalPositions[slot] = vec4(cell, 0.0);
        return;
    }

    // MODE 1 — inverse resample. Decode this thread's dest cell from its linear
    // slot in the [side]³ cube, recenter to [-center, +center]³ — shifted +1 on
    // anchored axes: with anchor = -0.5 the dest cells (roundHalfUp(p - anchor),
    // p in [-r, r]) span the SAME cell count one cell higher, so shifting the
    // decode window covers them at zero dispatch growth (#2349).
    int side = dest_.y;
    int center = dest_.z;
    ivec3 d = ivec3(
        int(slot) % side,
        (int(slot) / side) % side,
        int(slot) / (side * side)
    );
    ivec3 revoxDestDecodeShift = ivec3(lessThan(anchor_.xyz, vec3(-0.25)));
    ivec3 destCell = d - ivec3(center) + revoxDestDecodeShift;

    // Inverse map: which source cell rotates onto this dest cell — via the
    // anchored points (see revoxSourceCellForDest), the same half-integer
    // classification the CPU mask twin uses.
    ivec3 src = revoxSourceCellForDest(destCell);
    ivec3 g = src - srcGridMin_.xyz;
    uint colorPacked = 0u;
    uint matFlagBone = 0u;
    uint reserved = 0u;
    if (all(greaterThanEqual(g, ivec3(0))) && all(lessThan(g, srcGridDims_.xyz))) {
        int li = g.x + srcGridDims_.x * (g.y + srcGridDims_.y * g.z);
        colorPacked = sourceGrid[3 * li];
        matFlagBone = sourceGrid[3 * li + 1];
        reserved = sourceGrid[3 * li + 2];
    }

    if (((colorPacked >> 24u) & 0xFFu) != 0u) {
        // Occupied dest cell — author position + color + active for this slot.
        // The raster position is the anchored POINT (cell + anchor), matching
        // mode 0's unrounded composed locals at identity (#2349): without the
        // anchor a rotating solid snapped to the shifted integer lattice.
        globalPositions[slot] = vec4(vec3(destCell) + anchor_.xyz, 0.0);
        // Author the ROTATED-frame face-occlusion mask from dest-grid adjacency,
        // replacing the stale unrotated source mask so stage 1/2 gate the
        // re-voxelize emit on faceIsExposed (no all-3-face bypass → no slot-tie
        // AO hatching). occ uses the kFaceOccluded* bit layout
        // (component_voxel.hpp): flagsByte bits 2..7 sit at matFlagBone 10..15.
        uint occ = 0u;
        if (revoxDestCovered(destCell + ivec3(-1, 0, 0))) occ |= (1u << 2);
        if (revoxDestCovered(destCell + ivec3( 1, 0, 0))) occ |= (1u << 3);
        if (revoxDestCovered(destCell + ivec3(0, -1, 0))) occ |= (1u << 4);
        if (revoxDestCovered(destCell + ivec3(0,  1, 0))) occ |= (1u << 5);
        if (revoxDestCovered(destCell + ivec3(0, 0, -1))) occ |= (1u << 6);
        if (revoxDestCovered(destCell + ivec3(0, 0,  1))) occ |= (1u << 7);
        matFlagBone = (matFlagBone & ~(0x3Fu << 10)) | (occ << 8);
        // Carry the source voxel's reserved word (per-trixel priority in
        // bits[1:0], #1960 / #2023) into the dest record verbatim — the same
        // word the static binding-6 upload writes; stage 2 masks `& 0x3u` at
        // decode. Without this the rotating fill hardcoded reserved 0, so a
        // spinning detached solid silently lost its per-trixel depth priority.
        destColors[slot] = Voxel(colorPacked, matFlagBone, reserved);
        atomicOr(activeMask[slot >> 5u], 1u << (slot & 31u));
    }
    // Empty dest cells: active bit stays 0 (CPU pre-cleared the window); their
    // position/color slots are never read (the compact skips inactive slots).
}
