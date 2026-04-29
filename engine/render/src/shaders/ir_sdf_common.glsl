// Shared SDF primitive evaluators for the trixel pipeline. Mirrors
// `engine/math/include/irreden/math/sdf.hpp` so the CPU bookkeeping
// (`IRMath::SDF::boundingHalf`, `IRMath::SDF::evaluate`, …) and both shader
// sites (shape rasterizer, sun-shadow analytic march) compute identical
// distances for any given primitive.
//
// Anyone touching one branch of `evaluateSDF` must update both
// `c_shapes_to_trixel.glsl` and `c_compute_sun_shadow.glsl` consumers and the
// Metal mirror in `metal/ir_sdf_common.metal`. Adding a new shape:
//   1. add the value in IRMath::SDF::ShapeType
//   2. add the SDF primitive function here (plus Metal mirror)
//   3. extend the switch in `evaluateSDF` here (plus Metal mirror)
//   4. extend the renderer's primitive helpers (sphere/cone/etc.) if needed

const uint SHAPE_BOX = 0u;
const uint SHAPE_SPHERE = 1u;
const uint SHAPE_CYLINDER = 2u;
const uint SHAPE_ELLIPSOID = 3u;
const uint SHAPE_CURVED_PANEL = 4u;
const uint SHAPE_WEDGE = 5u;
const uint SHAPE_TAPERED_BOX = 6u;
const uint SHAPE_CUSTOM_SDF = 7u;
const uint SHAPE_CONE = 8u;
const uint SHAPE_TORUS = 9u;

float sdfBox(vec3 p, vec3 halfExtents) {
    vec3 d = abs(p) - halfExtents;
    return max(d.x, max(d.y, d.z));
}

float sdfSphere(vec3 p, float radius) {
    return length(p) - radius;
}

float sdfCylinder(vec3 p, float radius, float halfHeight) {
    vec2 d = abs(vec2(length(p.xy), p.z)) - vec2(radius, halfHeight);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float sdfEllipsoid(vec3 p, vec3 radii) {
    if (radii.x <= 0.0 || radii.y <= 0.0 || radii.z <= 0.0) return 1.0;
    float k0 = length(p / radii);
    if (k0 < 1e-6) return -min(radii.x, min(radii.y, radii.z));
    float k1 = length(p / (radii * radii));
    return k0 * (k0 - 1.0) / k1;
}

float sdfTaperedBox(vec3 p, vec3 halfExtents, float taper) {
    float taperFactor = mix(
        1.0,
        taper,
        clamp((p.z + halfExtents.z) / (2.0 * halfExtents.z), 0.0, 1.0)
    );
    vec3 scaled = vec3(p.xy / max(taperFactor, 0.001), p.z);
    return sdfBox(scaled, halfExtents);
}

float sdfCone(vec3 p, float baseRadius, float halfHeight) {
    float t = clamp((p.z + halfHeight) / (2.0 * halfHeight), 0.0, 1.0);
    float radiusAtZ = baseRadius * (1.0 - t);
    float dRadial = length(p.xy) - radiusAtZ;
    float dZ = abs(p.z) - halfHeight;
    float dOutside = length(max(vec2(dRadial, dZ), 0.0));
    float dInside = min(max(dRadial, dZ), 0.0);
    return dOutside + dInside;
}

float sdfTorus(vec3 p, float majorR, float minorR) {
    float q = length(p.xy) - majorR;
    return length(vec2(q, p.z)) - minorR;
}

float sdfWedge(vec3 p, vec3 halfExtents) {
    float boxD = sdfBox(p, halfExtents);
    float planeD = p.z - halfExtents.z * (1.0 - p.x / max(halfExtents.x, 0.001));
    return max(boxD, planeD);
}

float sdfCurvedPanel(vec3 p, vec3 halfExtents, float curvature) {
    float nx = p.x / max(halfExtents.x, 0.001);
    float zMid = curvature * halfExtents.x * nx * nx;
    float dThickness = abs(p.z - zMid) - halfExtents.z;
    float dX = abs(p.x) - halfExtents.x;
    float dY = abs(p.y) - halfExtents.y;
    float dOutside = length(max(vec3(dX, dY, dThickness), 0.0));
    float dInside = min(max(dX, max(dY, dThickness)), 0.0);
    return dOutside + dInside;
}

// Generic SDF dispatch. Returns the signed distance from `localPos` (already
// transformed into the shape's local frame) to the shape's surface.
float evaluateSDF(vec3 localPos, uint shapeType, vec4 params) {
    vec3 halfSize = params.xyz * 0.5;
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
