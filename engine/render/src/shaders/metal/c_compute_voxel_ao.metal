#include "ir_iso_common.metal"

// Mirrors shaders/c_compute_voxel_ao.glsl. Per-pixel ambient-occlusion
// compute. Samples the 3D occupancy grid for filled cells on the face's
// outside and writes an AO factor (0..1) into canvasAO.r.

constant int kOccupancyGridSize = 256;
constant int kOccupancyGridHalfExtent = 128;
constant int kEmptyDistanceEncoded = 65535;

// Mirrors `FrameDataSun` from ir_render_types.hpp. Only `aoEnabled` is
// consumed here; the rest of the layout must match so the same UBO can
// be bound by sun-shadow and lighting passes. Kept in lockstep with the
// GLSL counterpart at binding 29.
struct FrameDataSun {
    float4 sunDirection;
    float sunIntensity;
    float sunAmbient;
    int shadowsEnabled;
    int shapeCasterCount;
    int occupancyBoundsCount;
    int aoEnabled;
    int padding1;
    int padding2;
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

kernel void c_compute_voxel_ao(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSun &sunFrameData [[buffer(29)]],
    device const uint *occupancyBits [[buffer(28)]],
    texture2d<int, access::read> trixelDistances [[texture(0)]],
    texture2d<float, access::write> canvasAO [[texture(1)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int2 pixel = int2(globalId.xy);
    int2 size = int2(
        int(trixelDistances.get_width()),
        int(trixelDistances.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    int encoded = trixelDistances.read(uint2(pixel)).x;
    if (encoded >= kEmptyDistanceEncoded) {
        canvasAO.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }
    if (sunFrameData.aoEnabled == 0) {
        canvasAO.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }

    int face = encoded & 3;
    int rawDepth = encoded >> 2;

    // If the subdivision encoding ever shifts, update
    // c_voxel_to_trixel_stage_1.metal and c_voxel_to_trixel_stage_2.metal
    // in lockstep — all three shaders must agree on rawDepth scaling.
    int subdivisions = max(frameData.voxelRenderOptions.y, 1);
    float2 canvasOffset = (frameData.voxelRenderOptions.x != 0)
        ? frameData.frameCanvasOffset * float(subdivisions)
        : frameData.frameCanvasOffset;
    int2 isoRel =
        pixel - frameData.trixelCanvasOffsetZ1 - int2(floor(canvasOffset));

    float3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (frameData.voxelRenderOptions.x != 0) {
        pos3D /= float(subdivisions);
    }
    // `roundHalfUp` lives in ir_iso_common.metal and mirrors
    // `IRMath::roundHalfUp` on the CPU side (see
    // system_build_occupancy_grid.hpp). Both ends MUST agree on
    // half-integer voxel positions or the AO sample lands in a
    // different cell than the one the CPU populated.
    int3 surfaceVoxel = roundHalfUp(pos3D);

    // Face-outward + tangent axes shared with the lighting lambert via
    // `faceOutwardNormalI` in ir_iso_common.metal.
    int3 outward = faceOutwardNormalI(face);
    int3 t1;
    int3 t2;
    if (face == kZFace) {
        t1 = int3(1, 0, 0);
        t2 = int3(0, 1, 0);
    } else if (face == kXFace) {
        t1 = int3(0, 1, 0);
        t2 = int3(0, 0, 1);
    } else {
        t1 = int3(1, 0, 0);
        t2 = int3(0, 0, 1);
    }

    int3 baseOut = surfaceVoxel + outward;
    int occl = 0;
    if (occupancyGetBit(occupancyBits, baseOut.x + t1.x, baseOut.y + t1.y, baseOut.z + t1.z)) occl++;
    if (occupancyGetBit(occupancyBits, baseOut.x - t1.x, baseOut.y - t1.y, baseOut.z - t1.z)) occl++;
    if (occupancyGetBit(occupancyBits, baseOut.x + t2.x, baseOut.y + t2.y, baseOut.z + t2.z)) occl++;
    if (occupancyGetBit(occupancyBits, baseOut.x - t2.x, baseOut.y - t2.y, baseOut.z - t2.z)) occl++;

    // Each filled edge-neighbor darkens by 10%; all four caps at 60%
    // brightness keeps crease darkening visually subtle. Must stay in
    // lockstep with the 0.10 coefficient in c_compute_voxel_ao.glsl.
    float ao = 1.0 - float(occl) * 0.10;
    canvasAO.write(float4(ao, 0.0, 0.0, 0.0), uint2(pixel));
}
