#include "ir_iso_common.metal"

// Mirrors shaders/c_compute_sun_shadow.glsl. Per-pixel directional sun
// shadow compute — reconstructs the voxel-space surface position for
// each rasterized pixel, marches toward the sun through the 3D
// occupancy grid, and writes a 0..1 brightness factor into canvasSunShadow.r.

constant int kOccupancyGridSize = 256;
constant int kOccupancyGridHalfExtent = 128;
constant int kEmptyDistanceEncoded = 65535;
constant int kMaxShadowMarchSteps = 64;
constant float kShadowDarken = 0.45;

struct FrameDataSunShadow {
    float4 sunDirection;
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

kernel void c_compute_sun_shadow(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSunShadow &sunFrameData [[buffer(29)]],
    device const uint *occupancyBits [[buffer(28)]],
    texture2d<int, access::read> trixelDistances [[texture(0)]],
    texture2d<float, access::write> canvasSunShadow [[texture(1)]],
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
        canvasSunShadow.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }

    int rawDepth = encoded >> 2;

    int2 isoRel =
        pixel - frameData.trixelCanvasOffsetZ1 - int2(floor(frameData.frameCanvasOffset));

    int subdivisions = max(frameData.voxelRenderOptions.y, 1);
    float3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (frameData.voxelRenderOptions.x != 0) {
        pos3D /= float(subdivisions);
    }
    int3 surfaceVoxel = int3(round(pos3D));

    float3 sunDir = sunFrameData.sunDirection.xyz;
    float3 rayPos = float3(surfaceVoxel) + sunDir;
    bool shadowed = false;
    for (int step = 0; step < kMaxShadowMarchSteps; ++step) {
        int3 cell = int3(round(rayPos));
        int he = kOccupancyGridHalfExtent;
        if (cell.x < -he || cell.x >= he ||
            cell.y < -he || cell.y >= he ||
            cell.z < -he || cell.z >= he) {
            break;
        }
        if (occupancyGetBit(occupancyBits, cell.x, cell.y, cell.z)) {
            shadowed = true;
            break;
        }
        rayPos += sunDir;
    }

    float factor = shadowed ? kShadowDarken : 1.0;
    canvasSunShadow.write(float4(factor, 0.0, 0.0, 0.0), uint2(pixel));
}
