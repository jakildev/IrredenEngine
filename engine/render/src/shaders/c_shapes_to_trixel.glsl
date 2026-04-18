#version 460 core

// Iso-projected SDF surface finding: iterate 2D iso-space footprint, solve
// for the front surface analytically along the (1,1,1) depth axis.
// Each thread handles one trixel pixel and evaluates all three faces.
//
// Future work: this analytical surface math could be reused in a direct
// fragment shader path, eliminating the intermediate canvas entirely.

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_constants.glsl"

layout(std140, binding = 23) uniform ShapesFrameData {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 canvasSize;
    uniform int shapeCount;
    uniform int passIndex;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
};

struct ShapeDescriptor {
    vec4 worldPosition;
    vec4 params;
    uint shapeType;
    uint color;
    uint entityId;
    uint jointIndex;
    uint flags;
    uint lodLevel;
    uint _pad0;
    uint _pad1;
};

layout(std430, binding = 20) readonly buffer ShapeBuffer {
    ShapeDescriptor shapes[];
};

layout(std430, binding = 21) readonly buffer JointBuffer {
    vec4 jointData[];
};

layout(std430, binding = 22) readonly buffer AnimBuffer {
    vec4 animData[];
};

layout(r32i, binding = 1) uniform iimage2D triangleCanvasDistances;
layout(rgba8, binding = 0) writeonly uniform image2D triangleCanvasColors;
layout(rg32ui, binding = 2) writeonly uniform uimage2D triangleCanvasEntityIds;

const uint SHAPE_BOX = 0u;
const uint SHAPE_SPHERE = 1u;
const uint SHAPE_CYLINDER = 2u;
const uint SHAPE_ELLIPSOID = 3u;
const uint SHAPE_CURVED_PANEL = 4u;
const uint SHAPE_WEDGE = 5u;
const uint SHAPE_TAPERED_BOX = 6u;
const uint SHAPE_CONE = 8u;
const uint SHAPE_TORUS = 9u;

const uint FLAG_HOLLOW = 1u;
const uint FLAG_VISIBLE = 8u;
const uint FLAG_CHECKERBOARD = 32u;
const uint FLAG_DEPTH_COLOR = 64u;

// FP→int snap guard for analytical depth solvers. dEntry/dIntExit are
// FMA-reordered across GPU scheduling, so a mathematically-integer entry
// of 5.0 can arrive as 4.9999999 or 5.0000001 on different frames, and
// ceil() flips between 5 and 6 → ±1 surfaceD jitter → static-scene flicker
// along shape silhouettes and checker cell boundaries. Biasing the ceil
// input by a small negative epsilon collapses both neighborhoods of
// noise-around-integer into the same integer output, eliminating the
// flip without meaningfully altering non-knife-edge behavior.
const float kCeilBiasEpsilon = 1.0e-3;
int stableCeilToInt(float x) {
    return int(ceil(x - kCeilBiasEpsilon));
}
// SDF carve-threshold jitter guard. The "sdf <= 0.5" carve test flips
// in/out on FMA noise when the evaluated SDF lands exactly at ±0.5.
// Biasing the thresholds slightly outward makes borderline voxels
// consistently included across frames, eliminating parity flips on
// checker cells that sit on a surface-d boundary.
const float kSdfBiasEpsilon = 1.0e-3;

// Per-tile descriptor stream. CPU batches every tile of every visible
// shape into this SSBO, then issues one dispatchCompute(totalTiles, 1, 1).
// Each workgroup handles one 8×8 iso-pixel tile; shapeIndex selects which
// shape in the ShapeBuffer it belongs to, and tileIsoOrigin is that tile's
// iso-space origin (already pre-aligned on CPU).
struct ShapeTileDescriptor {
    int shapeIndex;
    int _pad0;
    ivec2 tileIsoOrigin;
};

layout(std430, binding = 30) readonly buffer ShapeTileBuffer {
    ShapeTileDescriptor tiles[];
};

// ---------------------------------------------------------------
// O(1) analytical depth-axis intersection.
//
// p(d) = isoToLocal3D(isoRel, d) is linear in d (slope 1/3 per axis):
//   x(d) = (2d - 3*isoX - isoY) / 6
//   y(d) = (2d + 3*isoX - isoY) / 6
//   z(d) = (d + isoY) / 3
//
// Box: slab intersection of 3 linear intervals in d.
// Sphere/circle/ellipse: quadratic in d.
// Each returns the nearest integer depth where sdf <= 0.5
// (and sdf >= -0.5 for hollow shapes).
// ---------------------------------------------------------------

// Classic HSV->RGB used for per-voxel depth coloring.  h,s,v all in [0,1].
vec3 hsvToRgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// SDF evaluation (same functions as before, used for general fallback)
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
    float taperFactor = mix(1.0, taper, clamp((p.z + halfExtents.z) / (2.0 * halfExtents.z), 0.0, 1.0));
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
    float ny = p.y / max(halfExtents.y, 0.001);
    float zMid = curvature * halfExtents.x * nx * nx;
    float dThickness = abs(p.z - zMid) - halfExtents.z;
    float dX = abs(p.x) - halfExtents.x;
    float dY = abs(p.y) - halfExtents.y;
    float dOutside = length(max(vec3(dX, dY, dThickness), 0.0));
    float dInside = min(max(dX, max(dY, dThickness)), 0.0);
    return dOutside + dInside;
}

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

// Axis-aligned slab intersection: depth interval [dEntry, dExit] where
// |x(d)| <= hExt.x AND |y(d)| <= hExt.y AND |z(d)| <= hExt.z.
bool boxSlabIntersect(float isoX, float isoY, vec3 hExt,
                      out float dEntry, out float dExit) {
    float dxLo = (-6.0 * hExt.x + 3.0 * isoX + isoY) * 0.5;
    float dxHi = ( 6.0 * hExt.x + 3.0 * isoX + isoY) * 0.5;
    float dyLo = (-6.0 * hExt.y - 3.0 * isoX + isoY) * 0.5;
    float dyHi = ( 6.0 * hExt.y - 3.0 * isoX + isoY) * 0.5;
    float dzLo = -3.0 * hExt.z - isoY;
    float dzHi =  3.0 * hExt.z - isoY;

    dEntry = max(dxLo, max(dyLo, dzLo));
    dExit  = min(dxHi, min(dyHi, dzHi));
    return dEntry <= dExit;
}

// Z-height slab: depth interval where |z(d)| <= hZ.
void zSlabInterval(float isoY, float hZ, out float dLo, out float dHi) {
    dLo = -3.0 * hZ - isoY;
    dHi =  3.0 * hZ - isoY;
}

// XY circular cross-section: depth interval where x(d)^2 + y(d)^2 <= R^2.
// Since (x - y) = -isoX is constant along the depth axis, the constraint
// reduces to: (2d - isoY)^2 / 18 + isoX^2 / 2 <= R^2.
bool circleDepthInterval(float isoX, float isoY, float R,
                         out float dLo, out float dHi) {
    float disc = 18.0 * R * R - 9.0 * isoX * isoX;
    if (disc < 0.0) return false;
    float halfRange = sqrt(disc);
    dLo = (isoY - halfRange) * 0.5;
    dHi = (isoY + halfRange) * 0.5;
    return true;
}

// O(1) box depth intersection via slab intersection of 3 axis constraints.
int boxDepthIntersect(ivec2 isoRel, vec3 halfExtents, bool hollow) {
    float isoX = float(isoRel.x);
    float isoY = float(isoRel.y);

    float dEntry, dExit;
    if (!boxSlabIntersect(isoX, isoY, halfExtents + vec3(0.5), dEntry, dExit)) {
        return kInvalidDepth;
    }

    if (!hollow) {
        int candidate = stableCeilToInt(dEntry);
        if (float(candidate) > dExit) return kInvalidDepth;
        vec3 p = isoToLocal3D(isoRel, float(candidate));
        if (sdfBox(p, halfExtents) <= 0.5 + kSdfBiasEpsilon) return candidate;
        if (float(candidate + 1) <= dExit) return candidate + 1;
        return kInvalidDepth;
    }

    // Exclude interior where ALL |p.c| < halfExtents.c - 0.5 (sdf < -0.5).
    vec3 hInt = halfExtents - vec3(0.5);
    float dIntEntry = 1.0, dIntExit = 0.0;
    if (hInt.x > 0.0 && hInt.y > 0.0 && hInt.z > 0.0) {
        boxSlabIntersect(isoX, isoY, hInt, dIntEntry, dIntExit);
    }

    int candidate = stableCeilToInt(dEntry);
    if (float(candidate) > dExit) return kInvalidDepth;
    if (dIntEntry > dIntExit || float(candidate) <= dIntEntry) return candidate;
    candidate = stableCeilToInt(dIntExit);
    if (float(candidate) <= dExit) return candidate;
    return kInvalidDepth;
}

// O(1) sphere depth intersection via quadratic closest-approach solve.
int sphereDepthIntersect(ivec2 isoRel, float radius, bool hollow) {
    float R = radius + 0.5;
    vec3 p0 = isoToLocal3D(isoRel, 0.0);
    // dp/dd = (1/3, 1/3, 1/3), |dp/dd|^2 = 1/3.
    // Closest approach: t0 = -dot(p0, dp/dd) / |dp/dd|^2 = -(p0.x+p0.y+p0.z).
    float tClosest = -(p0.x + p0.y + p0.z);
    vec3 pClosest = isoToLocal3D(isoRel, tClosest);
    float perpDistSq = dot(pClosest, pClosest);

    if (perpDistSq > R * R) return kInvalidDepth;

    // |p(d)|^2 = perpDistSq + (d - tClosest)^2 / 3.
    // Solve for |p(d)| = R: halfChord = sqrt(3 * (R^2 - perpDistSq)).
    float halfChord = sqrt(3.0 * (R * R - perpDistSq));
    float tEntry = tClosest - halfChord;
    float tExit  = tClosest + halfChord;

    if (!hollow) {
        int candidate = stableCeilToInt(tEntry);
        if (float(candidate) > tExit) return kInvalidDepth;
        vec3 p = isoToLocal3D(isoRel, float(candidate));
        if (sdfSphere(p, radius) <= 0.5 + kSdfBiasEpsilon) return candidate;
        if (float(candidate + 1) <= tExit) return candidate + 1;
        return kInvalidDepth;
    }

    // Inner sphere exclusion: |p| < radius - 0.5.
    float Rint = radius - 0.5;
    float tIntEntry = tExit + 1.0, tIntExit = tEntry - 1.0;
    if (Rint > 0.0 && perpDistSq <= Rint * Rint) {
        float intHalfChord = sqrt(3.0 * (Rint * Rint - perpDistSq));
        tIntEntry = tClosest - intHalfChord;
        tIntExit  = tClosest + intHalfChord;
    }

    int entryBase = stableCeilToInt(tEntry);
    for (int i = 0; i < 2; i++) {
        int candidate = entryBase + i;
        if (float(candidate) > min(tIntEntry, tExit)) break;
        vec3 p = isoToLocal3D(isoRel, float(candidate));
        float sdf = sdfSphere(p, radius);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) return candidate;
    }
    int exitBase = stableCeilToInt(tIntExit);
    for (int i = 0; i < 2; i++) {
        int candidate = exitBase + i;
        if (float(candidate) > tExit) break;
        vec3 p = isoToLocal3D(isoRel, float(candidate));
        float sdf = sdfSphere(p, radius);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) return candidate;
    }
    return kInvalidDepth;
}

// O(1) cylinder depth intersection: z-height slab + xy-circle quadratic.
int cylinderDepthIntersect(ivec2 isoRel, float radius, float halfHeight,
                           bool hollow) {
    float isoX = float(isoRel.x);
    float isoY = float(isoRel.y);

    float dCircLo, dCircHi;
    if (!circleDepthInterval(isoX, isoY, radius + 0.5, dCircLo, dCircHi)) {
        return kInvalidDepth;
    }
    float dZLo, dZHi;
    zSlabInterval(isoY, halfHeight + 0.5, dZLo, dZHi);

    float dEntry = max(dCircLo, dZLo);
    float dExit  = min(dCircHi, dZHi);
    if (dEntry > dExit) return kInvalidDepth;

    if (!hollow) {
        int candidate = stableCeilToInt(dEntry);
        if (float(candidate) > dExit) return kInvalidDepth;
        vec3 p = isoToLocal3D(isoRel, float(candidate));
        if (sdfCylinder(p, radius, halfHeight) <= 0.5 + kSdfBiasEpsilon) return candidate;
        if (float(candidate + 1) <= dExit) return candidate + 1;
        return kInvalidDepth;
    }

    float Rint = radius - 0.5;
    float Hint = halfHeight - 0.5;
    float dIntEntry = dExit + 1.0, dIntExit = dEntry - 1.0;
    if (Rint > 0.0 && Hint > 0.0) {
        float diCircLo, diCircHi;
        if (circleDepthInterval(isoX, isoY, Rint, diCircLo, diCircHi)) {
            float diZLo, diZHi;
            zSlabInterval(isoY, Hint, diZLo, diZHi);
            dIntEntry = max(diCircLo, diZLo);
            dIntExit  = min(diCircHi, diZHi);
        }
    }

    int entryBase = stableCeilToInt(dEntry);
    for (int i = 0; i < 2; i++) {
        int candidate = entryBase + i;
        if (float(candidate) > min(dIntEntry, dExit)) break;
        vec3 p = isoToLocal3D(isoRel, float(candidate));
        float sdf = sdfCylinder(p, radius, halfHeight);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) return candidate;
    }
    int exitBase = stableCeilToInt(dIntExit);
    for (int i = 0; i < 2; i++) {
        int candidate = exitBase + i;
        if (float(candidate) > dExit) break;
        vec3 p = isoToLocal3D(isoRel, float(candidate));
        float sdf = sdfCylinder(p, radius, halfHeight);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) return candidate;
    }
    return kInvalidDepth;
}

// O(1) ellipsoid depth intersection via quadratic on the scaled-ellipsoid bound.
// Uses a conservative envelope (radii + 0.5) to compute an analytical entry/exit
// interval, then verifies with the actual IQ-style ellipsoid SDF.
int ellipsoidDepthIntersect(ivec2 isoRel, vec3 radii, bool hollow) {
    float isoX = float(isoRel.x);
    float isoY = float(isoRel.y);

    // Solve (x/Rx)^2 + (y/Ry)^2 + (z/Rz)^2 <= 1 for R = radii + 0.5.
    // Each scaled component is linear in d: si(d) = ai*d + bi.
    vec3 R = radii + vec3(0.5);
    float ax = 1.0 / (3.0 * R.x);
    float bx = (-3.0 * isoX - isoY) / (6.0 * R.x);
    float ay = 1.0 / (3.0 * R.y);
    float by = (3.0 * isoX - isoY) / (6.0 * R.y);
    float az = 1.0 / (3.0 * R.z);
    float bz = isoY / (3.0 * R.z);

    float A = ax*ax + ay*ay + az*az;
    float B = 2.0 * (ax*bx + ay*by + az*bz);
    float C = bx*bx + by*by + bz*bz - 1.0;

    float disc = B*B - 4.0*A*C;
    if (disc < 0.0) return kInvalidDepth;

    float sqrtDisc = sqrt(disc);
    float inv2A = 0.5 / A;
    float dEntry = (-B - sqrtDisc) * inv2A;
    float dExit  = (-B + sqrtDisc) * inv2A;

    if (!hollow) {
        int entryBase = stableCeilToInt(dEntry);
        for (int i = 0; i < 3; i++) {
            int candidate = entryBase + i;
            if (float(candidate) > dExit) return kInvalidDepth;
            vec3 p = isoToLocal3D(isoRel, float(candidate));
            if (sdfEllipsoid(p, radii) <= 0.5 + kSdfBiasEpsilon) return candidate;
        }
        return kInvalidDepth;
    }

    // Inner ellipsoid exclusion with radii - 0.5.
    vec3 Rint = radii - vec3(0.5);
    float dIntEntry = dExit + 1.0, dIntExit = dEntry - 1.0;
    if (Rint.x > 0.0 && Rint.y > 0.0 && Rint.z > 0.0) {
        float iax = 1.0 / (3.0 * Rint.x);
        float ibx = (-3.0 * isoX - isoY) / (6.0 * Rint.x);
        float iay = 1.0 / (3.0 * Rint.y);
        float iby = (3.0 * isoX - isoY) / (6.0 * Rint.y);
        float iaz = 1.0 / (3.0 * Rint.z);
        float ibz = isoY / (3.0 * Rint.z);

        float iA = iax*iax + iay*iay + iaz*iaz;
        float iB = 2.0 * (iax*ibx + iay*iby + iaz*ibz);
        float iC = ibx*ibx + iby*iby + ibz*ibz - 1.0;

        float iDisc = iB*iB - 4.0*iA*iC;
        if (iDisc >= 0.0) {
            float iSqrt = sqrt(iDisc);
            float iInv2A = 0.5 / iA;
            dIntEntry = (-iB - iSqrt) * iInv2A;
            dIntExit  = (-iB + iSqrt) * iInv2A;
        }
    }

    int entryBase = stableCeilToInt(dEntry);
    for (int i = 0; i < 3; i++) {
        int candidate = entryBase + i;
        if (float(candidate) > min(dIntEntry, dExit)) break;
        vec3 p = isoToLocal3D(isoRel, float(candidate));
        float sdf = sdfEllipsoid(p, radii);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) return candidate;
    }
    int exitBase = stableCeilToInt(dIntExit);
    for (int i = 0; i < 3; i++) {
        int candidate = exitBase + i;
        if (float(candidate) > dExit) break;
        vec3 p = isoToLocal3D(isoRel, float(candidate));
        float sdf = sdfEllipsoid(p, radii);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) return candidate;
    }
    return kInvalidDepth;
}

// General SDF depth search (fallback for shapes without O(1) intersection).
// Depth is LOCAL (relative to shape origin), consistent with O(1) functions.
int generalDepthSearch(ivec2 isoRel, uint shapeType, vec4 params, bool hollow,
                       float dExtent) {
    int dMin = int(floor(-dExtent));
    int dMax = int(ceil(dExtent));
    for (int d = dMin; d <= dMax; d += 1) {
        vec3 p = isoToLocal3D(isoRel, float(d));
        float sdf = evaluateSDF(p, shapeType, params);
        if (sdf <= 0.5 + kSdfBiasEpsilon &&
            (!hollow || sdf >= -0.5 - kSdfBiasEpsilon)) {
            return d;
        }
    }
    return kInvalidDepth;
}

// O(1) surface depth dispatcher.  Accepts pre-scaled parameters so the
// same analytical intersections work at any subdivision resolution.
// All returned depths are LOCAL (relative to shape origin).
int findSurfaceDepth(ivec2 isoRel, uint shapeType, vec4 params, uint flags,
                     float dExtent) {
    bool hollow = (flags & FLAG_HOLLOW) != 0u;
    vec3 halfSize = params.xyz * 0.5;

    if (shapeType == SHAPE_BOX) {
        return boxDepthIntersect(isoRel, halfSize, hollow);
    }
    if (shapeType == SHAPE_SPHERE) {
        return sphereDepthIntersect(isoRel, params.x, hollow);
    }
    if (shapeType == SHAPE_CYLINDER) {
        return cylinderDepthIntersect(isoRel, params.x, halfSize.z, hollow);
    }
    if (shapeType == SHAPE_ELLIPSOID) {
        return ellipsoidDepthIntersect(isoRel, halfSize, hollow);
    }
    return generalDepthSearch(isoRel, shapeType, params, hollow, dExtent);
}

void main() {
    ShapeTileDescriptor tile = tiles[gl_WorkGroupID.x];
    int shapeIndex = tile.shapeIndex;
    ivec2 isoOrigin = tile.tileIsoOrigin;
    ShapeDescriptor shape = shapes[shapeIndex];

    vec3 worldPos = shape.worldPosition.xyz;
    ivec3 origin = ivec3(round(worldPos));

    int renderMode = voxelRenderOptions.x;
    int subdivisions = max(voxelRenderOptions.y, 1);
    // Smooth mode degenerates to snap when there is nothing to smooth.  At
    // sub == 1 the analytical path would paint a 2x3 diamond at every iso
    // pixel (both parities), producing overlapping diamonds that alias into
    // the voxel-pool tiling.  Route sub==1 through the exact lattice walk
    // used by snap mode so shapes at minimum zoom match C_VoxelSetNew output
    // trixel-for-trixel.
    bool smoothMode = (renderMode != 0) && (subdivisions > 1);
    int sub = smoothMode ? subdivisions : 1;

    // For BOX shapes, params.xyz is the voxel count per axis.  Convert to
    // continuous extent (voxelCount - 1) so the SDF surface lands exactly on
    // the outermost integer voxel positions, matching C_VoxelSetNew semantics.
    ivec3 originScaled = origin * sub;
    vec3 effectiveSize = (shape.shapeType == SHAPE_BOX)
        ? shape.params.xyz - 1.0
        : shape.params.xyz;
    vec4 paramsScaled = vec4(effectiveSize * float(sub), shape.params.w);

    // Compute shape-specific bounding half-extent so that both the iso
    // early-exit and the generalDepthSearch range cover the full shape.
    vec3 boundingHalf;
    uint st = shape.shapeType;
    if (st == SHAPE_SPHERE) {
        boundingHalf = vec3(paramsScaled.x);
    } else if (st == SHAPE_CYLINDER || st == SHAPE_CONE) {
        boundingHalf = vec3(paramsScaled.x, paramsScaled.x,
                            paramsScaled.z * 0.5);
    } else if (st == SHAPE_TORUS) {
        float xyR = paramsScaled.x + paramsScaled.y;
        boundingHalf = vec3(xyR, xyR, paramsScaled.y);
    } else if (st == SHAPE_CURVED_PANEL) {
        vec3 hs = paramsScaled.xyz * 0.5;
        hs.z += abs(paramsScaled.w) * hs.x;
        boundingHalf = hs;
    } else {
        boundingHalf = paramsScaled.xyz * 0.5;
    }
    ivec3 extentScaled = ivec3(ceil(boundingHalf)) + ivec3(1);

    ivec2 originIsoScaled = pos3DtoPos2DIso(originScaled);
    ivec2 isoExtentScaled = ivec2(
        extentScaled.x + extentScaled.y,
        extentScaled.x + extentScaled.y + 2 * extentScaled.z);

    float dExtent = float(extentScaled.x + extentScaled.y + extentScaled.z);

    ivec2 pixelCoord = isoOrigin + ivec2(gl_LocalInvocationID.xy);

    ivec2 frameOffset = trixelCanvasOffsetZ1 +
        ivec2(floor(frameCanvasOffset * float(sub)));

    ivec2 baseCanvasPixel = frameOffset + pixelCoord;
    if (baseCanvasPixel.x < -3 || baseCanvasPixel.x >= canvasSize.x + 3 ||
        baseCanvasPixel.y < -3 || baseCanvasPixel.y >= canvasSize.y + 3) {
        return;
    }

    ivec2 isoPixelRel = pixelCoord - originIsoScaled;

    if (abs(isoPixelRel.x) > isoExtentScaled.x + 2 ||
        abs(isoPixelRel.y) > isoExtentScaled.y + 2) {
        return;
    }

    int surfaceD;

    // In snapped mode (sub=1) the shape must behave like discrete voxels and
    // match the CPU voxel-pool carve exactly.  The analytical findSurfaceDepth
    // is unreliable here because (a) its fast paths evaluate the SDF at the
    // non-lattice analytical entry point, not at integer voxel centers, and
    // (b) its generalDepthSearch uses integer-d but non-lattice column points
    // as well.  Both can miss the true front-most lattice voxel on a column.
    //
    // So in snap mode we ignore findSurfaceDepth entirely and walk the ENTIRE
    // iso-column lattice from -dExtent to +dExtent.  Integer voxels along an
    // iso column live on a sublattice: only iso pixels with (isoX + isoY)
    // even have voxels, and the valid depths satisfy d ≡ -isoY (mod 3),
    // spaced by 3.  The first hit whose integer-voxel SDF is inside the 0.5
    // carve threshold is the winner — this is exactly what CPU carving does.
    if (!smoothMode) {
        if (((isoPixelRel.x + isoPixelRel.y) & 1) != 0) return;

        int isoY = isoPixelRel.y;
        int dMin = int(floor(-dExtent)) - 3;
        int dMax = int(ceil(dExtent)) + 3;
        int rem = ((dMin + isoY) % 3 + 3) % 3;
        int dStart = dMin + ((3 - rem) % 3);

        bool found = false;
        int validD = 0;
        for (int d = dStart; d <= dMax; d += 3) {
            vec3 p = isoToLocal3D(isoPixelRel, float(d));
            ivec3 voxelPos = ivec3(round(p));
            if (pos3DtoPos2DIso(voxelPos) != isoPixelRel) continue;
            if (evaluateSDF(vec3(voxelPos), shape.shapeType, paramsScaled) <= 0.5) {
                validD = voxelPos.x + voxelPos.y + voxelPos.z;
                found = true;
                break;
            }
        }
        if (!found) return;
        surfaceD = validD;
    } else {
        surfaceD = findSurfaceDepth(
            isoPixelRel, shape.shapeType, paramsScaled, shape.flags,
            dExtent);
        if (surfaceD == kInvalidDepth) return;
    }

    int originDistance = originScaled.x + originScaled.y + originScaled.z;
    int baseDepth = surfaceD + originDistance;
    vec4 baseColor = unpackColor(shape.color);

    if ((shape.flags & FLAG_DEPTH_COLOR) != 0u) {
        // Normalize local iso-depth over the shape's *visible* iso-depth
        // range so each shape gets a full near/far gradient independent of
        // world position.  In this iso convention the camera looks from the
        // −x−y−z direction, so smaller d = x+y+z is *closer* to camera.
        // For a box of half h the front corner is d=-3h, and the visible
        // back edges sit at d=+h — a visible window [-3h, +h] of width 4h.
        // Map front → t=0 (red) and back → t=1 (blue).
        //
        // dExtent above includes a +1 per-axis safety margin for the
        // lattice walk; use the unpadded boundingHalf sum here instead so
        // the hue range isn't compressed.
        float dColor = boundingHalf.x + boundingHalf.y +
                       boundingHalf.z;
        float denomC = max((4.0 / 3.0) * dColor, 1.0);
        float t = clamp(
            (float(surfaceD) + dColor) / denomC, 0.0, 1.0);
        baseColor.rgb = hsvToRgb(vec3(0.66 * t, 1.0, 1.0));
    } else if ((shape.flags & FLAG_CHECKERBOARD) != 0u) {
        // Checker at EFFECTIVE-voxel (scaled-integer) granularity. As sub
        // increases (zoom or subdivision), the cube gets more cells and
        // the pattern refines — there's no collapse back to physical-voxel
        // size. Pure integer math from the iso-projection inverse so the
        // output is bit-exact across frames (no FMA flicker).
        //
        // Derivation: given scaled iso (iX, iY) and scaled depth d,
        //   6·x_scaled = 2d − 3·iX − iY
        //   6·y_scaled = 2d + 3·iX − iY
        //   6·z_scaled = 2d + 2·iY
        // Round each numerator by 6 half-away-from-zero so adjacent
        // pixels straddling 0 get symmetric cell phase.
        int iX = isoPixelRel.x;
        int iY = isoPixelRel.y;
        int d  = surfaceD;
        int nx6 = 2 * d - 3 * iX - iY;
        int ny6 = 2 * d + 3 * iX - iY;
        int nz6 = 2 * d + 2 * iY;
        int sx = (nx6 >= 0) ? (nx6 + 3) / 6 : -((-nx6 + 3) / 6);
        int sy = (ny6 >= 0) ? (ny6 + 3) / 6 : -((-ny6 + 3) / 6);
        int sz = (nz6 >= 0) ? (nz6 + 3) / 6 : -((-nz6 + 3) / 6);
        if (((sx + sy + sz) & 1) != 0) {
            baseColor.rgb *= 0.55;
        }
    }

    for (int face = 0; face < 3; face++) {
        int depthEncoded = encodeDepthWithFace(baseDepth, face);
        vec4 col = adjustColorForFace(baseColor, face);

        for (int subPixel = 0; subPixel < 2; subPixel++) {
            ivec2 offset = faceOffset_2x3(face, subPixel);
            ivec2 canvasPixel = baseCanvasPixel + offset;

            if (!isInsideCanvas(canvasPixel, canvasSize)) continue;

            if (passIndex == 0) {
                imageAtomicMin(triangleCanvasDistances, canvasPixel,
                               depthEncoded);
            } else {
                int stored = imageLoad(triangleCanvasDistances,
                                       canvasPixel).x;
                if (depthEncoded == stored) {
                    imageStore(triangleCanvasColors, canvasPixel, col);
                    imageStore(triangleCanvasEntityIds, canvasPixel,
                               uvec4(shape.entityId, 0u, 0u, 0u));
                }
            }
        }
    }
}
