#include <metal_stdlib>
using namespace metal;

// Mirrors shaders/c_propagate_light_volume.glsl. One iteration of the
// distance-tracked light dilation — alpha encodes residual strength
// from the seed (1.0 at the source, decremented by `stepFalloff` per
// Manhattan step, 0.0 past the radius). The cell picks whichever
// air-neighbor candidate has the highest residual alpha so the closest
// light wins overlap regions.

constant int kOccupancyGridSize = 256;
constant int kOccupancyGridHalfExtent = 128;

struct LightVolumeParams {
    int gridSize;
    int halfExtent;
    int lightCount;
    float stepFalloff;
};

inline bool occupancyGetBit(device const uint *occupancyBits, int wx, int wy, int wz) {
    int he = kOccupancyGridHalfExtent;
    if (wx < -he || wx >= he || wy < -he || wy >= he || wz < -he || wz >= he) {
        return false;
    }
    uint x = uint(wx + he);
    uint y = uint(wy + he);
    uint z = uint(wz + he);
    uint flat =
        (z * uint(kOccupancyGridSize) + y) * uint(kOccupancyGridSize) + x;
    uint bits = occupancyBits[flat >> 5u];
    return ((bits >> (flat & 31u)) & 1u) == 1u;
}

kernel void c_propagate_light_volume(
    texture3d<float, access::read> lightVolumeRead [[texture(0)]],
    texture3d<float, access::write> lightVolumeWrite [[texture(1)]],
    device const uint *occupancyBits [[buffer(28)]],
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
    const int3 worldCell = cell - int3(params.halfExtent);

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
        if (occupancyGetBit(occupancyBits, nWorld.x, nWorld.y, nWorld.z)) {
            continue;
        }
        const float4 nv = lightVolumeRead.read(uint3(nCell));
        const float candidateAlpha = nv.a - params.stepFalloff;
        if (candidateAlpha <= 0.0f) {
            continue;
        }
        if (candidateAlpha > best.a) {
            best = float4(nv.rgb, candidateAlpha);
        }
    }

    lightVolumeWrite.write(best, uint3(cell));
}
