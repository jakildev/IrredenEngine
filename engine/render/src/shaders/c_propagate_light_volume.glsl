#version 460 core

// One iteration of the GPU light dilation. Reads the previous
// frame's seed (or prior iteration's output) from `lightVolumeRead`
// and writes the dilated result into `lightVolumeWrite`. The host
// system runs this dispatch `kLightVolumePropagateIterations` times
// per frame, swapping the read/write bindings between iterations
// via the ping-pong pair on `C_CanvasLightVolume`.
//
// Per-cell rule (distance-tracked linear falloff):
//   • rgb stores the emissive color of the closest reaching light.
//   • alpha stores residual strength in [0, 1] — 1.0 at the seed,
//     decremented by `stepFalloff` per Manhattan step. alpha == 0 means
//     "this cell is past the light's radius and contributes nothing".
//   • Each cell picks the candidate (self or air-neighbor) with the
//     highest residual alpha — i.e. whichever wavefront has the most
//     strength left at this cell. Closest light dominates overlap
//     regions; per-channel mixing of overlapping lights is deferred
//     to a follow-up pass.
//   • Solid neighbors are skipped (light cannot propagate THROUGH
//     occluders) but solid cells themselves can still receive light
//     from adjacent air cells, so wall surfaces light up correctly.
//
// Memory pattern: 6 image reads + 6 occupancy bit reads per thread
// at 128³ cells × 32 iterations — well below the 71 ms CPU BFS this
// replaces. The occupancy lookups touch the same `OccupancyGrid` SSBO
// consumed by AO so no new producer is required for this PR.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 4) in;

const int kOccupancyGridSize = 256;
const int kOccupancyGridHalfExtent = 128;

layout(rgba8, binding = 0) readonly uniform image3D lightVolumeRead;
layout(rgba8, binding = 1) writeonly uniform image3D lightVolumeWrite;

// Phase 1c (#360): camera-anchored layout — header carries the world
// origin, then the bitfield. See c_compute_voxel_ao.glsl for the
// rationale on embedding the header in the SSBO.
layout(std430, binding = 28) readonly buffer OccupancyGrid {
    ivec4 occupancyWorldOrigin;
    uint occupancyBits[];
};

layout(std140, binding = 23) uniform LightVolumeParams {
    int gridSize;
    int halfExtent;
    int lightCount;
    float stepFalloff;
    // Phase 1c (#360): world voxel the volume is centered on this frame.
    // `worldCell = (cell - halfExtent) + lightVolumeWorldOrigin.xyz` maps
    // a local volume cell back to its world voxel for occupancy lookups.
    ivec4 lightVolumeWorldOrigin;
};

bool occupancyGetBit(int wx, int wy, int wz) {
    int he = kOccupancyGridHalfExtent;
    int lx = wx - occupancyWorldOrigin.x;
    int ly = wy - occupancyWorldOrigin.y;
    int lz = wz - occupancyWorldOrigin.z;
    if (lx < -he || lx >= he || ly < -he || ly >= he || lz < -he || lz >= he) {
        return false;
    }
    uint x = uint(lx + he);
    uint y = uint(ly + he);
    uint z = uint(lz + he);
    uint flat =
        (z * uint(kOccupancyGridSize) + y) * uint(kOccupancyGridSize) + x;
    uint bits = occupancyBits[flat >> 5u];
    return ((bits >> (flat & 31u)) & 1u) == 1u;
}

void main() {
    const ivec3 cell = ivec3(gl_GlobalInvocationID.xyz);
    if (cell.x >= gridSize || cell.y >= gridSize || cell.z >= gridSize) {
        return;
    }

    vec4 best = imageLoad(lightVolumeRead, cell);
    // Phase 1c (#360): map the local volume cell back to world coords
    // through the camera-anchored origin so the per-neighbor occupancy
    // lookup queries the right cell of the (independently anchored)
    // occupancy grid.
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
        if (occupancyGetBit(nWorld.x, nWorld.y, nWorld.z)) {
            continue;
        }
        const vec4 nv = imageLoad(lightVolumeRead, nCell);
        const float candidateAlpha = nv.a - stepFalloff;
        if (candidateAlpha <= 0.0) {
            continue;
        }
        if (candidateAlpha > best.a) {
            best = vec4(nv.rgb, candidateAlpha);
        }
    }

    imageStore(lightVolumeWrite, cell, best);
}
