#version 450 core

// Detached re-voxelize GPU fill.
// Fills the shared global-position SSBO (binding 5) — and, in inverse mode, the
// color (6) + active-mask (8) SSBOs — for a DETACHED_REVOXELIZE pool so the
// canvas rasterizes its rotated solid through CARDINAL/static frame data (no 2D
// forward-scatter deform). Two dispatch modes (RevoxelizeParams.dest_.w):
//
//  MODE 0 — IDENTITY / source path. One thread per LIVE SOURCE voxel; the thread
//    writes its resident composed local (rotated + rounded under a non-identity
//    rotation) to binding 5. The CPU uploads color + active for these
//    source-indexed slots, so this mode authors only binding 5.
//
//  MODE 1 — INVERSE RESAMPLE. One thread per DEST cell of the dispatch's cell
//    groups. A pool hosts one or more rigid voxel sets, each a CELL GROUP with
//    its own pose, source grid and contiguous range of dest slots. Forward
//    scatter (mode 0 under rotation) is NOT surjective onto the rotated
//    lattice, so some covered dest cells would get no source voxel (holes).
//    Inverse resampling dispatches over the DEST lattice and pulls: dest cell
//    `c` of group `g` inverse-maps to source cell
//    `roundHalfUp(R_g⁻¹·(c + destOffset_g) - anchor_g)`; if that source cell is
//    occupied (in the group's slice of the source grid, binding 9) the thread
//    authors `position[slot]=c + phase`, `color[slot]=srcColor`, and sets the
//    per-slot active bit. Surjective by construction → hole-free at every
//    size. The shared compact → stage1 → stage2 raster reads
//    position[i]/color[i]/active[i] and rasterizes active slots in both modes;
//    in this mode slot `i` means "dest cell i" instead of "source voxel i".
//    Two groups may author the same dest cell from different slots; the raster
//    resolves that like any other equal-depth pair.
//
// `rotateByQuat` / `rotateByInverseQuat` / `roundHalfUp` are the shared CPU↔GPU
// helpers in ir_iso_common.glsl (CPU mirrors IRMath::rotateVectorByQuat /
// IRMath::roundVec3HalfUp), so CPU, GLSL and Metal classify half-integers the
// same. MODE 1 ALSO authors the ROTATED-frame face-occlusion mask from dest-grid
// adjacency within the group (the GPU twin of REBUILD_GRID_VOXELS' CPU mask),
// so stage 1/2 gate the re-voxelize emit on `faceIsExposed` exactly like the
// GRID path.

#include "ir_iso_common.glsl"

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Mirrors IRRender::kRevoxelizeGroupsPerDispatch.
#define REVOXELIZE_GROUPS_PER_DISPATCH 32

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

// binding 9 — this pool's source occupancy+color grids (inverse lookup), one
// per cell group, concatenated. Three uints per source cell: [3k] = colorPacked
// (occupied iff alpha byte != 0), [3k+1] = materialFlagBone, [3k+2] = reserved
// (per-trixel priority carrier). Keyed by `srcCell - srcGridMin` past the
// group's first word.
layout(std430, binding = 9) readonly buffer RevoxelizeSourceGrid {
    uint sourceGrid[];
};

// binding 17 — this pool's resident authored locals (.xyz = composed local).
// Read only by the identity / source path (mode 0).
layout(std430, binding = 17) readonly buffer ResidentLocalsBuffer {
    vec4 residentLocals[];
};

struct RevoxelizeGroup {
    vec4 rotation;      // group rotation in the canvas frame, (qx, qy, qz, qw)
    vec4 destOffset;    // xyz: group-centered point = dest cell + destOffset
    vec4 anchor;        // xyz = half-cell anchor: solid point = cell + anchor
    ivec4 destBase;     // xyz = dest cell of the group's first slot, w = dest side
    ivec4 srcGridMin;   // xyz = source grid min cell, w = the group's first dest slot
    ivec4 srcGridDims;  // xyz = source grid dims, w = the grid's first word
};

// binding 16 — per-frame params: dispatch-domain descriptor + group poses.
layout(std140, binding = 16) uniform RevoxelizeParams {
    vec4 canvasRotation_; // mode 0: (qx, qy, qz, qw); identity = (0,0,0,1)
    ivec4 dest_;          // x = thread count, y = group count, z = first dest slot, w = inverse mode
    vec4 phase_;          // xyz = canvas lattice phase: raster position = cell + phase
    RevoxelizeGroup groups_[REVOXELIZE_GROUPS_PER_DISPATCH];
};

// The solid's true points sit at `cell + anchor` (an even-sized centered axis
// authors at half-integers; roundHalfUp places its source cells +0.5 off, so
// anchor is -0.5 there, 0 on odd axes). The inverse resample must rotate the
// anchored POINTS, not the raw lattice cells — mapping cells directly shifts
// the whole rotated raster by a constant half cell per even axis.
// Source cell for the group-centered point p: roundHalfUp(R⁻¹·p - anchor).
ivec3 revoxSourceCellForDest(ivec3 destCell, int group) {
    vec3 destPoint = vec3(destCell) + groups_[group].destOffset.xyz;
    return roundHalfUp(
        rotateByInverseQuat(destPoint, groups_[group].rotation) - groups_[group].anchor.xyz
    );
}

// Word index of source cell `src` in the group's grid, or -1 outside it.
int revoxSourceWord(ivec3 src, int group) {
    ivec3 g = src - groups_[group].srcGridMin.xyz;
    ivec3 dims = groups_[group].srcGridDims.xyz;
    if (any(lessThan(g, ivec3(0))) || any(greaterThanEqual(g, dims))) {
        return -1;
    }
    return groups_[group].srcGridDims.w + 3 * (g.x + dims.x * (g.y + dims.y * g.z));
}

// Is dest cell `c` covered by the group? Inverse-map to source + check
// occupancy — the GPU twin of REBUILD_GRID_VOXELS' dest-grid adjacency probe.
bool revoxDestCovered(ivec3 c, int group) {
    int word = revoxSourceWord(revoxSourceCellForDest(c, group), group);
    if (word < 0) {
        return false;
    }
    return ((sourceGrid[word] >> 24u) & 0xFFu) != 0u;
}

void main() {
    uint workGroupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    uint threadIndex = workGroupIndex * 64u + gl_LocalInvocationID.x;
    if (threadIndex >= uint(dest_.x)) {
        return;
    }

    if (dest_.w == 0) {
        // MODE 0 — identity / source path. Slot == source voxel. Identity passes
        // the composed local through unrounded: it can sit at a half-integer
        // anchor, which roundHalfUp would shift.
        vec3 composed = residentLocals[threadIndex].xyz;
        vec3 cell;
        if (canvasRotation_ == vec4(0.0, 0.0, 0.0, 1.0)) {
            cell = composed;
        } else {
            cell = vec3(roundHalfUp(rotateByQuat(composed, canvasRotation_)));
        }
        globalPositions[threadIndex] = vec4(cell, 0.0);
        return;
    }

    // MODE 1 — inverse resample. The groups' dest-slot ranges are contiguous
    // and ascending, so the owning group is the last one starting at or before
    // this slot.
    int slot = dest_.z + int(threadIndex);
    int group = 0;
    for (int i = 1; i < dest_.y; ++i) {
        if (slot >= groups_[i].srcGridMin.w) {
            group = i;
        }
    }

    // Decode this thread's dest cell from its position in the group's [side]³
    // cube. `destBase` is the CPU-resolved first cell of the smallest window
    // that holds the group under any rotation at its current translation.
    int side = groups_[group].destBase.w;
    int slotInGroup = slot - groups_[group].srcGridMin.w;
    ivec3 d = ivec3(
        slotInGroup % side,
        (slotInGroup / side) % side,
        slotInGroup / (side * side)
    );
    ivec3 destCell = d + groups_[group].destBase.xyz;

    // Inverse map: which source cell rotates onto this dest cell — via the
    // anchored points, the same half-integer classification the CPU mask twin
    // uses.
    int word = revoxSourceWord(revoxSourceCellForDest(destCell, group), group);
    uint colorPacked = 0u;
    uint matFlagBone = 0u;
    uint reserved = 0u;
    if (word >= 0) {
        colorPacked = sourceGrid[word];
        matFlagBone = sourceGrid[word + 1];
        reserved = sourceGrid[word + 2];
    }

    if (((colorPacked >> 24u) & 0xFFu) != 0u) {
        // The raster position is the lattice POINT (cell + phase), matching
        // mode 0's unrounded composed locals at identity.
        globalPositions[slot] = vec4(vec3(destCell) + phase_.xyz, 0.0);
        // Author the ROTATED-frame face-occlusion mask from dest-grid adjacency,
        // replacing the unrotated source mask, so stage 1/2 gate the re-voxelize
        // emit on faceIsExposed. occ uses the kFaceOccluded* bit layout
        // (component_voxel.hpp): flagsByte bits 2..7 sit at matFlagBone 10..15.
        uint occ = 0u;
        if (revoxDestCovered(destCell + ivec3(-1, 0, 0), group)) occ |= (1u << 2);
        if (revoxDestCovered(destCell + ivec3( 1, 0, 0), group)) occ |= (1u << 3);
        if (revoxDestCovered(destCell + ivec3(0, -1, 0), group)) occ |= (1u << 4);
        if (revoxDestCovered(destCell + ivec3(0,  1, 0), group)) occ |= (1u << 5);
        if (revoxDestCovered(destCell + ivec3(0, 0, -1), group)) occ |= (1u << 6);
        if (revoxDestCovered(destCell + ivec3(0, 0,  1), group)) occ |= (1u << 7);
        matFlagBone = (matFlagBone & ~(0x3Fu << 10)) | (occ << 8);
        // Carry the source voxel's reserved word (per-trixel priority in
        // bits[1:0]) into the dest record verbatim — the same word the static
        // binding-6 upload writes; stage 2 masks `& 0x3u` at decode.
        destColors[slot] = Voxel(colorPacked, matFlagBone, reserved);
        atomicOr(activeMask[uint(slot) >> 5u], 1u << (uint(slot) & 31u));
    }
    // Empty dest cells: active bit stays 0 (CPU pre-cleared the window); their
    // position/color slots are never read (the compact skips inactive slots).
}
