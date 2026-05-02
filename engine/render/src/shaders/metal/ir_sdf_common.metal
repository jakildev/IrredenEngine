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

#endif // IR_SDF_COMMON_METAL_INCLUDED
