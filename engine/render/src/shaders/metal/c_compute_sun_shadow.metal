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
constant int kMaxAnalyticShapeMarchSteps = 32;
constant float kAnalyticShadowSurfaceThreshold = 0.35;

constant uint SHAPE_BOX          = 0u;
constant uint SHAPE_SPHERE       = 1u;
constant uint SHAPE_CYLINDER     = 2u;
constant uint SHAPE_ELLIPSOID    = 3u;
constant uint SHAPE_CURVED_PANEL = 4u;
constant uint SHAPE_WEDGE        = 5u;
constant uint SHAPE_TAPERED_BOX  = 6u;
constant uint SHAPE_CONE         = 8u;
constant uint SHAPE_TORUS        = 9u;
constant uint FLAG_HOLLOW        = 1u;

struct FrameDataSun {
    float4 sunDirection;
    float sunIntensity;
    float sunAmbient;
    int shadowsEnabled;
    int shapeCasterCount;
    int4 padding;
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

inline float sdfBox(float3 p, float3 halfExtents) {
    const float3 d = abs(p) - halfExtents;
    return max(d.x, max(d.y, d.z));
}

inline float sdfSphere(float3 p, float radius) {
    return length(p) - radius;
}

inline float sdfCylinder(float3 p, float radius, float halfHeight) {
    const float2 d = abs(float2(length(p.xy), p.z)) - float2(radius, halfHeight);
    return min(max(d.x, d.y), 0.0) + length(max(d, float2(0.0)));
}

inline float sdfEllipsoid(float3 p, float3 radii) {
    if (radii.x <= 0.0 || radii.y <= 0.0 || radii.z <= 0.0) {
        return 1.0;
    }
    const float k0 = length(p / radii);
    if (k0 < 1e-6) {
        return -min(radii.x, min(radii.y, radii.z));
    }
    const float k1 = length(p / (radii * radii));
    return k0 * (k0 - 1.0) / k1;
}

inline float sdfTaperedBox(float3 p, float3 halfExtents, float taper) {
    const float taperFactor =
        mix(1.0, taper,
            clamp((p.z + halfExtents.z) / (2.0 * halfExtents.z), 0.0, 1.0));
    const float3 scaled = float3(p.xy / max(taperFactor, 0.001), p.z);
    return sdfBox(scaled, halfExtents);
}

inline float sdfCone(float3 p, float baseRadius, float halfHeight) {
    const float t = clamp((p.z + halfHeight) / (2.0 * halfHeight), 0.0, 1.0);
    const float radiusAtZ = baseRadius * (1.0 - t);
    const float dRadial = length(p.xy) - radiusAtZ;
    const float dZ = abs(p.z) - halfHeight;
    const float dOutside = length(max(float2(dRadial, dZ), float2(0.0)));
    const float dInside = min(max(dRadial, dZ), 0.0);
    return dOutside + dInside;
}

inline float sdfTorus(float3 p, float majorR, float minorR) {
    const float q = length(p.xy) - majorR;
    return length(float2(q, p.z)) - minorR;
}

inline float sdfWedge(float3 p, float3 halfExtents) {
    const float boxD = sdfBox(p, halfExtents);
    const float planeD =
        p.z - halfExtents.z * (1.0 - p.x / max(halfExtents.x, 0.001));
    return max(boxD, planeD);
}

inline float sdfCurvedPanel(float3 p, float3 halfExtents, float curvature) {
    const float nx = p.x / max(halfExtents.x, 0.001);
    const float zMid = curvature * halfExtents.x * nx * nx;
    const float dThickness = abs(p.z - zMid) - halfExtents.z;
    const float dX = abs(p.x) - halfExtents.x;
    const float dY = abs(p.y) - halfExtents.y;
    const float dOutside = length(max(float3(dX, dY, dThickness), float3(0.0)));
    const float dInside = min(max(dX, max(dY, dThickness)), 0.0);
    return dOutside + dInside;
}

inline float evaluateShapeSDF(float3 localPos, uint shapeType, float4 params) {
    const float3 halfSize = params.xyz * 0.5;
    switch (shapeType) {
        case SHAPE_BOX:          return sdfBox(localPos, halfSize);
        case SHAPE_SPHERE:       return sdfSphere(localPos, params.x);
        case SHAPE_CYLINDER:     return sdfCylinder(localPos, params.x, halfSize.z);
        case SHAPE_ELLIPSOID:    return sdfEllipsoid(localPos, halfSize);
        case SHAPE_TAPERED_BOX:  return sdfTaperedBox(localPos, halfSize, params.w);
        case SHAPE_CONE:         return sdfCone(localPos, params.x, halfSize.z);
        case SHAPE_TORUS:        return sdfTorus(localPos, params.x, params.y);
        case SHAPE_WEDGE:        return sdfWedge(localPos, halfSize);
        case SHAPE_CURVED_PANEL: return sdfCurvedPanel(localPos, halfSize, params.w);
        default:                 return sdfBox(localPos, halfSize);
    }
}

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
                evaluateShapeSDF(samplePos - center, shape.shapeType, shape.params);
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

    int face = encoded & 3;
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

    // The voxel-to-trixel rasterizer encodes positions at the voxel CENTER
    // plane in the face's normal axis (Z face stays at the voxel-center z;
    // X face stays at center x; Y face stays at center y). For the SDF
    // path that's fine — pos3D already lies on the surface — but for the
    // voxel-pool path the reconstructed point is half a voxel inside the
    // surface. Shift along the face's outward normal so rays start at the
    // actual face surface. Without this shift, sub-voxel offsets on the
    // X / Y faces (where u, v span [0, 0.75] inward) make the very first
    // ray cell land back on the voxel itself and self-shadow.
    if (face == kZFace) {
        pos3D.z -= 0.5;
    } else if (face == kXFace) {
        pos3D.x += 0.5;
    } else { // kYFace
        pos3D.y += 0.5;
    }

    float3 sunDir = sunFrameData.sunDirection.xyz;
    uint selfEntityId = trixelEntityIds.read(uint2(pixel)).x;
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
