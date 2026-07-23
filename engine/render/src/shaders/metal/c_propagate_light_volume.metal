#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_propagate_light_volume.glsl. One iteration of the
// distance-tracked light dilation — alpha encodes residual strength
// from the seed (the CPU-computed seed residual at the source: 1.0
// in-window, distance-discounted for out-of-window lights clamped to
// the volume edge; decremented by `stepFalloff` per
// Manhattan step, 0.0 past the radius). The cell picks whichever
// air-neighbor candidate has the highest residual alpha so the closest
// light wins overlap regions.
//
// Blocker neighbors are skipped on the same rule as solid voxels: a cell
// marked in the light-blocker bitfield (rasterized from
// `C_ShapeDescriptor + C_LightBlocker(blocksLOS_=true)` entities by
// `system_build_light_occlusion_grid`) blocks point/spot light
// propagation. T-126 scoped this SSBO to the light-volume LOS path
// (AO migrated to screen-space sampling in T-091), so the struct name
// `LightOcclusionData` reflects the surviving consumer.

constant int kLightOcclusionGridSize = 256;
constant int kLightOcclusionGridHalfExtent = 128;
// Number of `uint` entries in one bitfield (256³ / 32). Voxel bits live
// at indices [0, kLightOcclusionBitfieldUintCount); light-blocker bits
// at [kLightOcclusionBitfieldUintCount, 2 * kLightOcclusionBitfieldUintCount).
// Must match `kLightOcclusionBitfieldUintCount` in
// `system_build_light_occlusion_grid.hpp`.
constant uint kLightOcclusionBitfieldUintCount =
    uint(kLightOcclusionGridSize) * uint(kLightOcclusionGridSize) *
    uint(kLightOcclusionGridSize) / 32u;

// Deliberate local copy of the shared layout in ir_world_lighting.metal (which
// the seed / lighting / overflow passes bind through): this is the one
// light-volume consumer that does not include that fragment, and pulling the
// light-source list + SPOT/ACES helpers into a kernel dispatched 32× a frame
// just to share a struct declaration is not worth it. Keep the two in lockstep.
struct LightVolumeParams {
    int gridSize;
    int halfExtent;
    int lightCount;
    float stepFalloff;
    // Phase 1c (#360): world voxel the volume is centered on this frame.
    int4 worldOriginVoxel;
};

// Phase 1c (#360): camera-anchored light-occlusion SSBO layout — header
// (worldOriginVoxel) followed by the voxel bitfield and the SDF-blocker
// bitfield (each `kLightOcclusionBitfieldUintCount` uints).
struct LightOcclusionData {
    int4 worldOriginVoxel;
    uint bits[1];
};

inline bool voxelOcclusionGetBit(
    device const LightOcclusionData *occlusion,
    int wx,
    int wy,
    int wz
) {
    int he = kLightOcclusionGridHalfExtent;
    int4 worldOrigin = occlusion->worldOriginVoxel;
    int lx = wx - worldOrigin.x;
    int ly = wy - worldOrigin.y;
    int lz = wz - worldOrigin.z;
    if (lx < -he || lx >= he || ly < -he || ly >= he || lz < -he || lz >= he) {
        return false;
    }
    uint x = uint(lx + he);
    uint y = uint(ly + he);
    uint z = uint(lz + he);
    uint flatIndex =
        (z * uint(kLightOcclusionGridSize) + y) * uint(kLightOcclusionGridSize) + x;
    uint bits = occlusion->bits[flatIndex >> 5u];
    return ((bits >> (flatIndex & 31u)) & 1u) == 1u;
}

// SDF-shape light blockers. Same camera-anchored layout as the voxel
// bitfield, indexed `kLightOcclusionBitfieldUintCount` `uint`s further
// into the same `bits[]` flexible-array tail.
inline bool lightBlockerGetBit(
    device const LightOcclusionData *occlusion,
    int wx,
    int wy,
    int wz
) {
    int he = kLightOcclusionGridHalfExtent;
    int4 worldOrigin = occlusion->worldOriginVoxel;
    int lx = wx - worldOrigin.x;
    int ly = wy - worldOrigin.y;
    int lz = wz - worldOrigin.z;
    if (lx < -he || lx >= he || ly < -he || ly >= he || lz < -he || lz >= he) {
        return false;
    }
    uint x = uint(lx + he);
    uint y = uint(ly + he);
    uint z = uint(lz + he);
    uint flatIndex =
        (z * uint(kLightOcclusionGridSize) + y) * uint(kLightOcclusionGridSize) + x;
    uint bits = occlusion->bits[kLightOcclusionBitfieldUintCount + (flatIndex >> 5u)];
    return ((bits >> (flatIndex & 31u)) & 1u) == 1u;
}

kernel void c_propagate_light_volume(
    texture3d<float, access::read> lightVolumeRead [[texture(0)]],
    texture3d<float, access::write> lightVolumeWrite [[texture(1)]],
    // Winning-light ID ping-pong (#2318): the winner's ID rides along with
    // its color so the consumer knows which light lit each cell (SPOT cone).
    texture3d<float, access::read> lightVolumeIdRead [[texture(2)]],
    texture3d<float, access::write> lightVolumeIdWrite [[texture(3)]],
    device const LightOcclusionData *occlusion [[buffer(28)]],
    constant LightVolumeParams &params [[buffer(23)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    const int3 cell = int3(globalId);
    if (cell.x >= params.gridSize ||
        cell.y >= params.gridSize ||
        cell.z >= params.gridSize) {
        return;
    }

    float4 best = lightVolumeRead.read(uint3(cell));
    // Winning-light ID travels with `best` — seeded from self, overwritten
    // whenever a neighbor candidate wins the residual contest below (#2318).
    // Skipped when no SPOT was seeded (the consumer never reads it); the branch
    // is coherent across the dispatch, so no-spot scenes pay no extra bandwidth.
    const bool carryId = params.worldOriginVoxel.w != 0;
    float4 bestId = carryId ? lightVolumeIdRead.read(uint3(cell)) : float4(0.0);
    // Phase 1c (#360): map the local volume cell back to world coords
    // through the camera-anchored origin so the per-neighbor light-
    // occlusion lookup queries the right cell of the (independently
    // anchored) light-occlusion grid.
    const int3 worldCell = (cell - int3(params.halfExtent)) + params.worldOriginVoxel.xyz;

    const int3 deltas[6] = {
        int3(1, 0, 0), int3(-1, 0, 0),
        int3(0, 1, 0), int3(0, -1, 0),
        int3(0, 0, 1), int3(0, 0, -1)
    };

    for (int n = 0; n < 6; ++n) {
        const int3 nCell = cell + deltas[n];
        if (nCell.x < 0 || nCell.x >= params.gridSize ||
            nCell.y < 0 || nCell.y >= params.gridSize ||
            nCell.z < 0 || nCell.z >= params.gridSize) {
            continue;
        }
        const int3 nWorld = worldCell + deltas[n];
        if (voxelOcclusionGetBit(occlusion, nWorld.x, nWorld.y, nWorld.z) ||
            lightBlockerGetBit(occlusion, nWorld.x, nWorld.y, nWorld.z)) {
            continue;
        }
        const float4 nv = lightVolumeRead.read(uint3(nCell));
        const float candidateAlpha = nv.a - params.stepFalloff;
        if (candidateAlpha <= 0.0f) {
            continue;
        }
        if (candidateAlpha > best.a) {
            best = float4(nv.rgb, candidateAlpha);
            // Carry the winning neighbor's ID so it stays in sync with the
            // color it just replaced. Only loaded on a win (≤6 extra reads).
            if (carryId) {
                bestId = lightVolumeIdRead.read(uint3(nCell));
            }
        }
    }

    lightVolumeWrite.write(best, uint3(cell));
    if (carryId) {
        lightVolumeIdWrite.write(bestId, uint3(cell));
    }
}
