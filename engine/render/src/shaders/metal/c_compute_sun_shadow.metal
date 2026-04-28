#include "ir_iso_common.metal"
#include "ir_sdf_common.metal"

// Mirrors shaders/c_compute_sun_shadow.glsl. Per-pixel directional sun
// shadow compute — reconstructs the voxel-space surface position for
// each rasterized pixel, marches toward the sun through the 3D
// occupancy grid, and writes a 0..1 brightness factor into canvasSunShadow.r.

constant int kOccupancyGridSize = 256;
constant int kOccupancyGridHalfExtent = 128;
constant int kEmptyDistanceEncoded = 65535;
constant int kMaxShadowMarchSteps = 64;
constant float kShadowDarken = 0.45;
constant int kMaxAnalyticShapeMarchSteps = 32;
constant float kAnalyticShadowSurfaceThreshold = 0.35;

// Shape-type constants (SHAPE_BOX, …) live in ir_sdf_common.metal.
constant uint FLAG_HOLLOW        = 1u;

struct FrameDataSun {
    float4 sunDirection;
    float sunIntensity;
    float sunAmbient;
    int shadowsEnabled;
    int shapeCasterCount;
    int occupancyBoundsCount;
    int padding0;
    int padding1;
    int padding2;
};

struct ShapeDescriptor {
    float4 worldPosition;
    float4 params;
    uint shapeType;
    uint color;
    uint entityId;
    uint jointIndex;
    uint flags;
    uint lodLevel;
    uint pad0;
    uint pad1;
};

// Mirrors GPUOccupancyEntityBounds in ir_render_types.hpp. See the GLSL
// shader for the rationale.
struct OccupancyEntityBounds {
    uint4 entityId;
    int4 minCell;
    int4 maxCell;
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

// SDF primitives and `evaluateSDF` live in ir_sdf_common.metal, shared with
// the shape rasterizer.

inline float shapeBoundingRadius(thread const ShapeDescriptor& shape) {
    const float3 halfSize = abs(shape.params.xyz) * 0.5;
    switch (shape.shapeType) {
        case SHAPE_SPHERE:    return shape.params.x + 0.5;
        case SHAPE_CYLINDER:  return length(float2(shape.params.x, halfSize.z)) + 0.5;
        case SHAPE_CONE:      return length(float2(shape.params.x, halfSize.z)) + 0.5;
        case SHAPE_TORUS:     return shape.params.x + shape.params.y + 0.5;
        default:              return length(halfSize) + abs(shape.params.w) + 0.5;
    }
}

inline bool rayIntersectsSphere(
    float3 origin,
    float3 dir,
    float3 center,
    float radius,
    thread float& tNear,
    thread float& tFar
) {
    const float3 oc = origin - center;
    const float b = dot(oc, dir);
    const float c = dot(oc, oc) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) {
        return false;
    }
    h = sqrt(h);
    tNear = -b - h;
    tFar = -b + h;
    return tFar > 0.0;
}

inline bool analyticShapeShadowHit(
    float3 rayOrigin,
    float3 rayDir,
    uint selfEntityId,
    constant FrameDataVoxelToTrixel& frameData,
    constant FrameDataSun& sunFrameData,
    device const ShapeDescriptor *shapeCasters
) {
    if (frameData.voxelRenderOptions.x == 0 || sunFrameData.shapeCasterCount <= 0) {
        return false;
    }

    const int subdivisions = max(frameData.voxelRenderOptions.y, 1);
    const float minStep = 1.0 / float(min(subdivisions, 4));

    for (int i = 0; i < sunFrameData.shapeCasterCount; ++i) {
        const ShapeDescriptor shape = shapeCasters[i];
        if (shape.entityId == selfEntityId) {
            continue;
        }

        const float3 center = shape.worldPosition.xyz;
        float tNear, tFar;
        if (!rayIntersectsSphere(
                rayOrigin, rayDir, center, shapeBoundingRadius(shape), tNear, tFar
            )) {
            continue;
        }

        float t = max(tNear, minStep);
        for (int step = 0; step < kMaxAnalyticShapeMarchSteps && t <= tFar; ++step) {
            const float3 samplePos = rayOrigin + rayDir * t;
            const float distance =
                evaluateSDF(samplePos - center, shape.shapeType, shape.params);
            const bool hollow = (shape.flags & FLAG_HOLLOW) != 0u;
            if ((!hollow && distance <= kAnalyticShadowSurfaceThreshold) ||
                (hollow && abs(distance) <= kAnalyticShadowSurfaceThreshold)) {
                return true;
            }
            t += max(distance * 0.75, minStep);
        }
    }
    return false;
}

kernel void c_compute_sun_shadow(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSun &sunFrameData [[buffer(29)]],
    device const uint *occupancyBits [[buffer(28)]],
    device const ShapeDescriptor *shapeCasters [[buffer(20)]],
    device const OccupancyEntityBounds *occupancyEntityBounds [[buffer(4)]],
    texture2d<int, access::read> trixelDistances [[texture(0)]],
    texture2d<float, access::write> canvasSunShadow [[texture(1)]],
    texture2d<uint, access::read> trixelEntityIds [[texture(2)]],
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
    if (sunFrameData.shadowsEnabled == 0) {
        canvasSunShadow.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }

    int rawDepth = encoded >> 2;

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

    float3 sunDir = sunFrameData.sunDirection.xyz;
    uint selfEntityId = trixelEntityIds.read(uint2(pixel)).x;

    // Look up this surface's voxel-pool bbox so the occupancy march below
    // can skip self-cells. Mirrors the analytic path's selfEntityId
    // exclusion. SDF surfaces and pixels where the entity didn't make it
    // into the bounds buffer simply fall through with the sentinel range,
    // matching no cell — i.e. behaving as if no self-exclusion is needed.
    int3 selfMin = int3(2147483647);
    int3 selfMax = int3(-2147483648);
    for (int i = 0; i < sunFrameData.occupancyBoundsCount; ++i) {
        if (occupancyEntityBounds[i].entityId.x == selfEntityId) {
            selfMin = occupancyEntityBounds[i].minCell.xyz;
            selfMax = occupancyEntityBounds[i].maxCell.xyz;
            break;
        }
    }

    float3 rayOrigin = pos3D + sunDir;
    bool shadowed = analyticShapeShadowHit(
        rayOrigin,
        sunDir,
        selfEntityId,
        frameData,
        sunFrameData,
        shapeCasters
    );

    float3 rayPos = rayOrigin;
    for (int step = 0; !shadowed && step < kMaxShadowMarchSteps; ++step) {
        // `roundHalfUp` lives in ir_iso_common.metal and mirrors
        // `IRMath::roundHalfUp` on the CPU side (see
        // system_build_occupancy_grid.hpp). The CPU populates cells with
        // round-half-up; the GPU march MUST sample with the same rule or
        // half-integer rays classify cells inconsistently.
        int3 cell = roundHalfUp(rayPos);
        int he = kOccupancyGridHalfExtent;
        if (cell.x < -he || cell.x >= he ||
            cell.y < -he || cell.y >= he ||
            cell.z < -he || cell.z >= he) {
            break;
        }
        bool isSelfCell =
            cell.x >= selfMin.x && cell.x <= selfMax.x &&
            cell.y >= selfMin.y && cell.y <= selfMax.y &&
            cell.z >= selfMin.z && cell.z <= selfMax.z;
        if (!isSelfCell && occupancyGetBit(occupancyBits, cell.x, cell.y, cell.z)) {
            shadowed = true;
            break;
        }
        rayPos += sunDir;
    }

    float factor = shadowed ? kShadowDarken : 1.0;
    canvasSunShadow.write(float4(factor, 0.0, 0.0, 0.0), uint2(pixel));
}
