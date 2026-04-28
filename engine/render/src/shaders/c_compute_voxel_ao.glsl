#version 460 core

// Per-pixel ambient-occlusion compute. For each pixel on a rasterized
// surface — voxel OR shape, since both write encoded face+depth via
// `encodeDepthWithFace` — samples the 3D occupancy grid for filled cells
// on the face's outside and writes an AO factor (0..1) to the canvas AO
// texture's .r channel.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

// Matches system_build_occupancy_grid.hpp's SSBO sizing. The grid is
// [z][y][x] row-major, bit at flat index (z*size + y)*size + x, centered
// so integer (0,0,0) lands at the middle of the bitfield.
const int kOccupancyGridSize = 256;
const int kOccupancyGridHalfExtent = 128;

// Same threshold LIGHTING_TO_TRIXEL uses for "empty pixel" — encoded
// distances >= 65535 mean the clear value was never overwritten. Well
// above any real rasterized depth since voxels are capped around ±128.
const int kEmptyDistanceEncoded = 65535;

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
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 1) writeonly uniform image2D canvasAO;

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
        imageStore(canvasAO, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }

    int face = encoded & 3;
    int rawDepth = encoded >> 2;

    // Reconstruct a voxel-space surface point. In POSITION_ONLY / FULL
    // subdivision modes rawDepth + iso coords are in sub-voxel units —
    // divide back to the integer voxel grid so occupancyGetBit reads from
    // the same coordinate space the build step wrote. If the subdivision
    // encoding ever shifts, update c_voxel_to_trixel_stage_1.glsl and
    // c_voxel_to_trixel_stage_2.glsl in lockstep — all three shaders
    // must agree on rawDepth scaling.
    int subdivisions = max(voxelRenderOptions.y, 1);
    vec2 canvasOffset = (voxelRenderOptions.x != 0)
        ? frameCanvasOffset * float(subdivisions)
        : frameCanvasOffset;
    ivec2 isoRel =
        pixel - trixelCanvasOffsetZ1 - ivec2(floor(canvasOffset));

    vec3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (voxelRenderOptions.x != 0) {
        pos3D /= float(subdivisions);
    }
    // `roundHalfUp` lives in ir_iso_common.glsl and mirrors
    // `IRMath::roundHalfUp` on the CPU side (see
    // system_build_occupancy_grid.hpp). Both ends MUST agree on
    // half-integer voxel positions or the AO sample lands in a
    // different cell than the one the CPU populated.
    ivec3 surfaceVoxel = roundHalfUp(pos3D);

    // Face-outward + the two tangent axes spanning the face plane.
    // Sampling happens one voxel out, offset along each tangent, to
    // detect edge-adjacent occluders. `faceOutwardNormalI` lives in
    // ir_iso_common.glsl and is shared with the lighting lambert
    // calculation so AO and shading agree on which way is "out".
    ivec3 outward = faceOutwardNormalI(face);
    ivec3 t1;
    ivec3 t2;
    if (face == kZFace) {
        t1 = ivec3(1, 0, 0);
        t2 = ivec3(0, 1, 0);
    } else if (face == kXFace) {
        t1 = ivec3(0, 1, 0);
        t2 = ivec3(0, 0, 1);
    } else {
        t1 = ivec3(1, 0, 0);
        t2 = ivec3(0, 0, 1);
    }

    ivec3 baseOut = surfaceVoxel + outward;
    int occl = 0;
    if (occupancyGetBit(baseOut.x + t1.x, baseOut.y + t1.y, baseOut.z + t1.z)) occl++;
    if (occupancyGetBit(baseOut.x - t1.x, baseOut.y - t1.y, baseOut.z - t1.z)) occl++;
    if (occupancyGetBit(baseOut.x + t2.x, baseOut.y + t2.y, baseOut.z + t2.z)) occl++;
    if (occupancyGetBit(baseOut.x - t2.x, baseOut.y - t2.y, baseOut.z - t2.z)) occl++;

    // Each filled edge-neighbor darkens by 10%; all four caps at 60%
    // brightness keeps crease darkening visually subtle.
    float ao = 1.0 - float(occl) * 0.10;
    imageStore(canvasAO, pixel, vec4(ao, 0.0, 0.0, 0.0));
}
