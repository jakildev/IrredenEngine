#version 460 core

// Per-pixel directional sun shadow compute. For each rasterized surface
// pixel (voxel OR shape) reconstructs the voxel-space surface position
// from the encoded distance texture, then marches from that position
// toward the sun through the 3D occupancy grid. If any solid voxel is
// hit within kMaxShadowMarchSteps the pixel is shadowed, otherwise lit.
// Result written as a 0..1 brightness factor into the R channel of the
// canvas sun-shadow texture, consumed later by LIGHTING_TO_TRIXEL.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

// Matches system_build_occupancy_grid.hpp's SSBO sizing.
const int kOccupancyGridSize = 256;
const int kOccupancyGridHalfExtent = 128;

// Must match `kEmptyDistanceEncoded` in c_compute_voxel_ao.glsl.
const int kEmptyDistanceEncoded = 65535;

// Max iso-march steps per pixel. 64 covers the common "building casting
// a shadow" scale while bounding per-frame work — the full pass is
// O(canvasPixels * steps) so tune here if the 1 ms budget slips.
const int kMaxShadowMarchSteps = 64;

// Darkening applied to fully-shadowed pixels. 0.45 leaves enough
// detail visible inside shadows; tweak here rather than in the lighting
// pass so the shadow texture stays the single source of truth.
const float kShadowDarken = 0.45;

layout(std430, binding = 28) readonly buffer OccupancyGrid {
    uint occupancyBits[];
};

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    uniform int _voxelDispatchPadding;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
};

layout(std140, binding = 29) uniform FrameDataSunShadow {
    // xyz = unit vector pointing from the world toward the sun; w unused.
    uniform vec4 sunDirection;
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 1) writeonly uniform image2D canvasSunShadow;

bool occupancyGetBit(int wx, int wy, int wz) {
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

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(trixelDistances);
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    int encoded = imageLoad(trixelDistances, pixel).x;
    if (encoded >= kEmptyDistanceEncoded) {
        imageStore(canvasSunShadow, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }

    int rawDepth = encoded >> 2;

    ivec2 isoRel =
        pixel - trixelCanvasOffsetZ1 - ivec2(floor(frameCanvasOffset));

    // Same reconstruction as c_compute_voxel_ao.glsl — keep the two in
    // lockstep so AO and shadow sample the same voxel cells.
    int subdivisions = max(voxelRenderOptions.y, 1);
    vec3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (voxelRenderOptions.x != 0) {
        pos3D /= float(subdivisions);
    }
    ivec3 surfaceVoxel = ivec3(round(pos3D));

    // Start the ray one voxel outside the surface in the sun direction to
    // avoid counting the surface voxel itself as its own occluder.
    vec3 rayPos = vec3(surfaceVoxel) + sunDirection.xyz;
    bool shadowed = false;
    for (int step = 0; step < kMaxShadowMarchSteps; ++step) {
        ivec3 cell = ivec3(round(rayPos));
        int he = kOccupancyGridHalfExtent;
        if (cell.x < -he || cell.x >= he ||
            cell.y < -he || cell.y >= he ||
            cell.z < -he || cell.z >= he) {
            // Left the grid without hitting anything — lit.
            break;
        }
        if (occupancyGetBit(cell.x, cell.y, cell.z)) {
            shadowed = true;
            break;
        }
        rayPos += sunDirection.xyz;
    }

    float factor = shadowed ? kShadowDarken : 1.0;
    imageStore(canvasSunShadow, pixel, vec4(factor, 0.0, 0.0, 0.0));
}
