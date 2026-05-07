#include "ir_iso_common.metal"

// Mirrors shaders/c_compute_voxel_ao.glsl. Per-pixel ambient-occlusion
// compute. Samples the 3D occupancy grid for filled cells on the face's
// outside and writes an AO factor (0..1) into canvasAO.r.

constant int kOccupancyGridSize = 256;
constant int kOccupancyGridHalfExtent = 128;
constant int kEmptyDistanceEncoded = 65535;

// Mirrors `FrameDataSun` from ir_render_types.hpp. Only `aoEnabled` is
// consumed here; the layout must match so the shared UBO at binding 29
// can be read by every consumer (BAKE_SUN_SHADOW_MAP owns the upload).
struct FrameDataSun {
    float4 sunDirection;
    float sunIntensity;
    float sunAmbient;
    int shadowsEnabled;
    int aoEnabled;
    float4 sunBasisU;
    float4 sunBasisV;
    float2 sunBufferOriginUV;
    float2 sunBufferTexelSize;
};

// Phase 1c (#360): camera-anchored occupancy SSBO layout — first 16
// bytes are the world origin, then the bitfield. The header is
// embedded in the SSBO (rather than a separate UBO) because Metal
// compute encoders share one `setBuffer(slot)` table per encoder, so a
// UBO at the same slot as an unrelated SSBO would clobber it. Mirrors
// `OccupancyGridHeader` in ir_render_types.hpp.
struct OccupancyData {
    int4 worldOriginVoxel;
    // Sentinel array — MSL doesn't allow `bits[]` flexible members, but
    // accessing past index 0 reads the buffer's actual contents. The
    // CPU sizes the SSBO to fit `kMaxOccupancyGridSideVoxels^3 / 8`
    // bytes after the header.
    uint bits[1];
};

inline bool occupancyGetBit(
    device const OccupancyData *occupancy,
    int wx,
    int wy,
    int wz
) {
    int he = kOccupancyGridHalfExtent;
    int4 worldOrigin = occupancy->worldOriginVoxel;
    int lx = wx - worldOrigin.x;
    int ly = wy - worldOrigin.y;
    int lz = wz - worldOrigin.z;
    if (lx < -he || lx >= he || ly < -he || ly >= he || lz < -he || lz >= he) {
        return false;
    }
    uint x = uint(lx + he);
    uint y = uint(ly + he);
    uint z = uint(lz + he);
    uint flat =
        (z * uint(kOccupancyGridSize) + y) * uint(kOccupancyGridSize) + x;
    uint bits = occupancy->bits[flat >> 5u];
    return ((bits >> (flat & 31u)) & 1u) == 1u;
}

kernel void c_compute_voxel_ao(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSun &sunFrameData [[buffer(29)]],
    device const OccupancyData *occupancy [[buffer(28)]],
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
    // R(-rasterYaw) compose recovers world-frame surface position and
    // outward / tangent vectors from the cardinal-rotated raster frame.
    // At cardinalIndex==0 the path collapses to master so yaw=0 stays
    // byte-identical.
    // Also computed inside trixelCanvasPixelToWorld3D; retained here for the
    // sampling-vector rotation below.
    int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    float3 pos3D = trixelCanvasPixelToWorld3D(
        pixel,
        rawDepth,
        frameData.trixelCanvasOffsetZ1,
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions,
        frameData.rasterYaw
    );
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
    if (cardinalIndex != 0) {
        outward = rotateCardinalZInvI(outward, cardinalIndex);
        t1 = rotateCardinalZInvI(t1, cardinalIndex);
        t2 = rotateCardinalZInvI(t2, cardinalIndex);
    }

    int3 baseOut = surfaceVoxel + outward;
    int occl = 0;
    if (occupancyGetBit(occupancy, baseOut.x + t1.x, baseOut.y + t1.y, baseOut.z + t1.z)) occl++;
    if (occupancyGetBit(occupancy, baseOut.x - t1.x, baseOut.y - t1.y, baseOut.z - t1.z)) occl++;
    if (occupancyGetBit(occupancy, baseOut.x + t2.x, baseOut.y + t2.y, baseOut.z + t2.z)) occl++;
    if (occupancyGetBit(occupancy, baseOut.x - t2.x, baseOut.y - t2.y, baseOut.z - t2.z)) occl++;

    // Each filled edge-neighbor darkens by 10%; all four caps at 60%
    // brightness keeps crease darkening visually subtle. Must stay in
    // lockstep with the 0.10 coefficient in c_compute_voxel_ao.glsl.
    float ao = 1.0 - float(occl) * 0.10;
    canvasAO.write(float4(ao, 0.0, 0.0, 0.0), uint2(pixel));
}
