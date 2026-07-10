#version 450 core

// One iteration of the GPU light dilation. Reads the previous
// frame's seed (or prior iteration's output) from `lightVolumeRead`
// and writes the dilated result into `lightVolumeWrite`. The host
// system runs this dispatch `kLightVolumePropagateIterations` times
// per frame, swapping the read/write bindings between iterations
// via the ping-pong pair on `C_CanvasLightVolume`.
//
// Per-cell rule (distance-tracked linear falloff):
//   • rgb stores the emissive color of the closest reaching light.
//   • alpha stores residual strength in [0, 1] — seeded at the light's
//     CPU-computed residual (1.0 in-window; distance-discounted for
//     out-of-window lights clamped to the volume edge), decremented by
//     `stepFalloff` per Manhattan step. alpha == 0 means
//     "this cell is past the light's radius and contributes nothing".
//   • Each cell picks the candidate (self or air-neighbor) with the
//     highest residual alpha — i.e. whichever wavefront has the most
//     strength left at this cell. Closest light dominates overlap
//     regions; per-channel mixing of overlapping lights is deferred
//     to a follow-up pass.
//   • Solid neighbors are skipped (light cannot propagate THROUGH
//     occluders) but solid cells themselves can still receive light
//     from adjacent air cells, so wall surfaces light up correctly.
//   • Blocker neighbors are skipped on the same rule: a cell marked in
//     the light-blocker bitfield (rasterized from `C_ShapeDescriptor +
//     C_LightBlocker(blocksLOS_=true)` entities by
//     `system_build_light_occlusion_grid`) blocks point/spot light
//     propagation just like a solid voxel, restoring the SDF-LOS
//     behaviour the GPU port lost in #359.
//
// Memory pattern: 6 image reads + 6 occlusion bit reads per thread
// at 128³ cells × 32 iterations — well below the 71 ms CPU BFS this
// replaces. The voxel and SDF-blocker bitfields live in the same SSBO
// (header + voxel bitfield + blocker bitfield), so the propagate shader
// does one extra `uint` load per neighbor — no new buffer slot is
// needed (Metal caps at 0–30 and every slot is already in use).
// T-126 scoped this SSBO down to the light-volume LOS path; AO migrated
// to screen-space sampling in T-091, and the `LightOcclusionGrid` name
// reflects that the data now feeds only this shader.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 4) in;

const int kLightOcclusionGridSize = 256;
const int kLightOcclusionGridHalfExtent = 128;
// Number of `uint` entries in one bitfield (256³ / 32). Voxel bits live
// at indices [0, kLightOcclusionBitfieldUintCount); light-blocker bits at
// [kLightOcclusionBitfieldUintCount, 2 * kLightOcclusionBitfieldUintCount).
// Must match `kLightOcclusionBitfieldUintCount` in
// `system_build_light_occlusion_grid.hpp`.
const uint kLightOcclusionBitfieldUintCount =
    uint(kLightOcclusionGridSize) * uint(kLightOcclusionGridSize) *
    uint(kLightOcclusionGridSize) / 32u;

layout(rgba8, binding = 0) readonly uniform image3D lightVolumeRead;
layout(rgba8, binding = 1) writeonly uniform image3D lightVolumeWrite;
// Winning-light ID ping-pong (#2318): the ID of whichever candidate wins a
// cell's residual contest rides along with its color, so the consumer knows
// which light lit the cell (for the SPOT cone factor). Swapped in lockstep
// with the color pair.
layout(rgba8, binding = 2) readonly uniform image3D lightVolumeIdRead;
layout(rgba8, binding = 3) writeonly uniform image3D lightVolumeIdWrite;

// Phase 1c (#360) / T-126: camera-anchored layout — header carries the
// world origin, then two parallel bitfields. The voxel-existence
// bitfield (set by `system_build_light_occlusion_grid` from live voxels)
// is followed by the SDF-blocker bitfield (set from `C_LightBlocker`
// shape entities). Both are consumed only by this shader.
layout(std430, binding = 28) readonly buffer LightOcclusionGrid {
    ivec4 occlusionWorldOrigin;
    uint occlusionBits[];
};

layout(std140, binding = 23) uniform LightVolumeParams {
    int gridSize;
    int halfExtent;
    int lightCount;
    float stepFalloff;
    // Phase 1c (#360): world voxel the volume is centered on this frame.
    // `worldCell = (cell - halfExtent) + lightVolumeWorldOrigin.xyz` maps
    // a local volume cell back to its world voxel for occupancy lookups.
    // `.w` is the has-SPOT flag (#2318): a coherent (whole-dispatch) branch
    // that skips the winning-light-ID image ops entirely when no SPOT was
    // seeded, so no-spot scenes pay zero extra propagate bandwidth.
    ivec4 lightVolumeWorldOrigin;
};

bool voxelOcclusionGetBit(int wx, int wy, int wz) {
    int he = kLightOcclusionGridHalfExtent;
    int lx = wx - occlusionWorldOrigin.x;
    int ly = wy - occlusionWorldOrigin.y;
    int lz = wz - occlusionWorldOrigin.z;
    if (lx < -he || lx >= he || ly < -he || ly >= he || lz < -he || lz >= he) {
        return false;
    }
    uint x = uint(lx + he);
    uint y = uint(ly + he);
    uint z = uint(lz + he);
    uint flatIndex =
        (z * uint(kLightOcclusionGridSize) + y) * uint(kLightOcclusionGridSize) + x;
    uint bits = occlusionBits[flatIndex >> 5u];
    return ((bits >> (flatIndex & 31u)) & 1u) == 1u;
}

// SDF-shape light blockers. Same camera-anchored layout as the voxel
// bitfield, indexed `kLightOcclusionBitfieldUintCount` `uint`s into the
// same `occlusionBits[]` array. Returns true when (wx, wy, wz) lies
// inside any `C_ShapeDescriptor` with `C_LightBlocker(blocksLOS_=true)`.
bool lightBlockerGetBit(int wx, int wy, int wz) {
    int he = kLightOcclusionGridHalfExtent;
    int lx = wx - occlusionWorldOrigin.x;
    int ly = wy - occlusionWorldOrigin.y;
    int lz = wz - occlusionWorldOrigin.z;
    if (lx < -he || lx >= he || ly < -he || ly >= he || lz < -he || lz >= he) {
        return false;
    }
    uint x = uint(lx + he);
    uint y = uint(ly + he);
    uint z = uint(lz + he);
    uint flatIndex =
        (z * uint(kLightOcclusionGridSize) + y) * uint(kLightOcclusionGridSize) + x;
    uint bits = occlusionBits[kLightOcclusionBitfieldUintCount + (flatIndex >> 5u)];
    return ((bits >> (flatIndex & 31u)) & 1u) == 1u;
}

void main() {
    const ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
    if (cell.x >= gridSize || cell.y >= gridSize || cell.z >= gridSize) {
        return;
    }

    vec4 best = imageLoad(lightVolumeRead, cell);
    // Winning-light ID travels with `best` — seeded from self, overwritten
    // whenever a neighbor candidate wins the residual contest below (#2318).
    // Skipped when no SPOT was seeded (the consumer never reads it), so
    // no-spot scenes do no extra id image traffic. `carryId` is uniform across
    // the dispatch, so the branch is coherent (near-free).
    const bool carryId = lightVolumeWorldOrigin.w != 0;
    vec4 bestId = carryId ? imageLoad(lightVolumeIdRead, cell) : vec4(0.0);
    // Phase 1c (#360): map the local volume cell back to world coords
    // through the camera-anchored origin so the per-neighbor light-
    // occlusion lookup queries the right cell of the (independently
    // anchored) light-occlusion grid.
    const ivec3 worldCell = (cell - ivec3(halfExtent)) + lightVolumeWorldOrigin.xyz;

    const ivec3 deltas[6] = ivec3[6](
        ivec3(1, 0, 0), ivec3(-1, 0, 0),
        ivec3(0, 1, 0), ivec3(0, -1, 0),
        ivec3(0, 0, 1), ivec3(0, 0, -1)
    );

    for (int n = 0; n < 6; ++n) {
        const ivec3 nCell = cell + deltas[n];
        if (nCell.x < 0 || nCell.x >= gridSize ||
            nCell.y < 0 || nCell.y >= gridSize ||
            nCell.z < 0 || nCell.z >= gridSize) {
            continue;
        }
        const ivec3 nWorld = worldCell + deltas[n];
        if (voxelOcclusionGetBit(nWorld.x, nWorld.y, nWorld.z) ||
            lightBlockerGetBit(nWorld.x, nWorld.y, nWorld.z)) {
            continue;
        }
        const vec4 nv = imageLoad(lightVolumeRead, nCell);
        const float candidateAlpha = nv.a - stepFalloff;
        if (candidateAlpha <= 0.0) {
            continue;
        }
        if (candidateAlpha > best.a) {
            best = vec4(nv.rgb, candidateAlpha);
            // Carry the winning neighbor's ID so it stays in sync with the
            // color it just replaced. Only loaded on a win (≤6 extra reads).
            if (carryId) {
                bestId = imageLoad(lightVolumeIdRead, nCell);
            }
        }
    }

    imageStore(lightVolumeWrite, cell, best);
    if (carryId) {
        imageStore(lightVolumeIdWrite, cell, bestId);
    }
}
