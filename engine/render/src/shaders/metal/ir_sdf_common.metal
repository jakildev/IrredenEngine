// Shared SDF primitive evaluators for the trixel pipeline.  Mirrors
// shaders/ir_sdf_common.glsl and the CPU-side `IRMath::SDF` namespace
// (engine/math/include/irreden/math/sdf.hpp).  The shape rasterizer
// (`c_shapes_to_trixel.metal`) and the sun-shadow shader
// (`c_compute_sun_shadow.metal`) both #include this header.
//
// Anyone touching one branch of `evaluateSDF` must update the GLSL mirror
// in shaders/ir_sdf_common.glsl and the CPU helpers in IRMath::SDF.
#ifndef IR_SDF_COMMON_METAL_INCLUDED
#define IR_SDF_COMMON_METAL_INCLUDED

#include <metal_stdlib>
using namespace metal;

constant uint SHAPE_BOX          = 0u;
constant uint SHAPE_SPHERE       = 1u;
constant uint SHAPE_CYLINDER     = 2u;
constant uint SHAPE_ELLIPSOID    = 3u;
constant uint SHAPE_CURVED_PANEL = 4u;
constant uint SHAPE_WEDGE        = 5u;
constant uint SHAPE_TAPERED_BOX  = 6u;
constant uint SHAPE_CUSTOM_SDF   = 7u;
constant uint SHAPE_CONE         = 8u;
constant uint SHAPE_TORUS        = 9u;

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
    const float3 scaled =
        float3(p.xy / max(taperFactor, 0.001), p.z);
    return sdfBox(scaled, halfExtents);
}

inline float sdfCone(float3 p, float baseRadius, float halfHeight) {
    const float t =
        clamp((p.z + halfHeight) / (2.0 * halfHeight), 0.0, 1.0);
    const float radiusAtZ = baseRadius * (1.0 - t);
    const float dRadial = length(p.xy) - radiusAtZ;
    const float dZ = abs(p.z) - halfHeight;
    const float dOutside =
        length(max(float2(dRadial, dZ), float2(0.0)));
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
    const float dOutside =
        length(max(float3(dX, dY, dThickness), float3(0.0)));
    const float dInside = min(max(dX, max(dY, dThickness)), 0.0);
    return dOutside + dInside;
}

// Generic SDF dispatch.  Returns the signed distance from `localPos`
// (already transformed into the shape's local frame) to the shape surface.
inline float evaluateSDF(float3 localPos, uint shapeType, float4 params) {
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

// |a*d + b| <= H solved for d. Degenerate (a == 0) returns either an empty
// or all-d slab depending on |b| vs H.
//
// Threshold 1e-6 catches FP near-degenerate slopes at irrational yaws near
// pi/4, where (cos-sin)/3 is tiny but nonzero (at exactly pi/4, cos==sin in
// IEEE-754 so (c-s)/3 == 0). A smaller threshold lets 1/|a| blow up past 1e6
// across that cluster. The +/-1e18 sentinel is an all-d slab the downstream
// min/max swallows.
inline bool slabFromLinear(
    float a,
    float b,
    float H,
    thread float& dLo,
    thread float& dHi
) {
    if (fabs(a) < 1e-6) {
        if (fabs(b) <= H) {
            dLo = -1e18;
            dHi = 1e18;
            return true;
        }
        return false;
    }
    const float invA = 1.0 / a;
    const float t1 = (-H - b) * invA;
    const float t2 = ( H - b) * invA;
    dLo = min(t1, t2);
    dHi = max(t1, t2);
    return true;
}

// Bounds and iso coordinates share the caller's density. No cell expansion,
// origin rounding, or depth quantization is applied here. The normal is in
// the unrotated box frame; outputs are valid only on a finite-box hit.
inline bool boxSurfaceIntervalYaw(
    float iX, float iY,
    float3 hExt,
    float yawC,
    float yawS,
    thread float& dEntry,
    thread float& dExit,
    thread float3& entryNormal
) {
    const float ax = (yawC - yawS) / 3.0;
    const float bx = -(yawC + yawS) * 0.5 * iX - (yawC - yawS) * iY / 6.0;
    const float ay = (yawC + yawS) / 3.0;
    const float by =  (yawC - yawS) * 0.5 * iX - (yawC + yawS) * iY / 6.0;
    const float az = 1.0 / 3.0;
    const float bz = iY / 3.0;

    float dxLo, dxHi, dyLo, dyHi, dzLo, dzHi;
    if (!slabFromLinear(ax, bx, hExt.x, dxLo, dxHi)) return false;
    if (!slabFromLinear(ay, by, hExt.y, dyLo, dyHi)) return false;
    if (!slabFromLinear(az, bz, hExt.z, dzLo, dzHi)) return false;

    dEntry = max(dxLo, max(dyLo, dzLo));
    dExit  = min(dxHi, min(dyHi, dzHi));
    if (dEntry > dExit) return false;
    // At a shared edge either plane is valid; ties choose X, then Y, then Z.
    if (dxLo >= dyLo && dxLo >= dzLo) {
        entryNormal = float3(ax > 0.0 ? -1.0 : 1.0, 0.0, 0.0);
    } else if (dyLo >= dzLo) {
        entryNormal = float3(0.0, ay > 0.0 ? -1.0 : 1.0, 0.0);
    } else {
        entryNormal = float3(0.0, 0.0, -1.0);
    }
    return true;
}

#endif // IR_SDF_COMMON_METAL_INCLUDED
