#include "ir_iso_common.metal"
#include "ir_constants.metal"

// Iso-projected SDF surface finding for analytical shapes.  Mirrors
// shaders/c_shapes_to_trixel.glsl.  Each workgroup handles one 8x8
// iso-pixel tile of one shape; pass 0 writes atomic-min depth taps into
// the scratch buffer (`distanceScratch`) and pass 1 reads back the
// resolved depth and stamps color + entity id into the trixel canvas.
//
// Workgroup dimensions: (8, 8, 1).

struct ShapesFrameData {
    float2 frameCanvasOffset;
    int2 trixelCanvasOffsetZ1;
    int2 canvasSize;
    int shapeCount;
    int passIndex;
    int2 voxelRenderOptions;
    int2 cullIsoMin;
    int2 cullIsoMax;
    float visualYaw;
    float _yawPadding0;
    float _yawPadding1;
    float _yawPadding2;
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

struct ShapeTileDescriptor {
    int shapeIndex;
    int pad0;
    int2 tileIsoOrigin;
};

constant uint SHAPE_BOX          = 0u;
constant uint SHAPE_SPHERE       = 1u;
constant uint SHAPE_CYLINDER     = 2u;
constant uint SHAPE_ELLIPSOID    = 3u;
constant uint SHAPE_CURVED_PANEL = 4u;
constant uint SHAPE_WEDGE        = 5u;
constant uint SHAPE_TAPERED_BOX  = 6u;
constant uint SHAPE_CONE         = 8u;
constant uint SHAPE_TORUS        = 9u;

constant uint FLAG_HOLLOW       = 1u;
constant uint FLAG_VISIBLE      = 8u;
constant uint FLAG_CHECKERBOARD = 32u;
constant uint FLAG_DEPTH_COLOR  = 64u;

// FP→int snap guard for analytical depth solvers. dEntry/dIntExit are
// FMA-reordered across GPU scheduling, so a mathematically-integer entry
// of 5.0 can arrive as 4.9999999 or 5.0000001 on different frames, and
// ceil() flips between 5 and 6 → ±1 surfaceD jitter → static-scene flicker
// along shape silhouettes and checker cell boundaries. Biasing the ceil
// input by a small negative epsilon collapses both noise neighborhoods
// into the same integer output.
constant float kCeilBiasEpsilon = 1.0e-3;
inline int stableCeilToInt(float x) {
    return int(ceil(x - kCeilBiasEpsilon));
}
// SDF carve-threshold jitter guard. The "sdf <= 0.5" carve test flips
// in/out on FMA noise when the evaluated SDF lands exactly at ±0.5.
// Biasing thresholds outward makes borderline voxels consistently included
// across frames, eliminating parity flips on checker cells on a surface-d
// boundary.
constant float kSdfBiasEpsilon = 1.0e-3;

// ---------- Color helpers ----------

inline float3 hsvToRgb(float3 c) {
    const float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    const float3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

// ---------- Generic SDF evaluators (fallback path) ----------

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
    const float ny = p.y / max(halfExtents.y, 0.001);
    const float zMid = curvature * halfExtents.x * nx * nx;
    const float dThickness = abs(p.z - zMid) - halfExtents.z;
    const float dX = abs(p.x) - halfExtents.x;
    const float dY = abs(p.y) - halfExtents.y;
    const float dOutside =
        length(max(float3(dX, dY, dThickness), float3(0.0)));
    const float dInside = min(max(dX, max(dY, dThickness)), 0.0);
    return dOutside + dInside;
}

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

// ---------- O(1) analytical depth-axis intersections ----------

inline bool boxSlabIntersect(
    float isoX,
    float isoY,
    float3 hExt,
    thread float& dEntry,
    thread float& dExit
) {
    const float dxLo = (-6.0 * hExt.x + 3.0 * isoX + isoY) * 0.5;
    const float dxHi = ( 6.0 * hExt.x + 3.0 * isoX + isoY) * 0.5;
    const float dyLo = (-6.0 * hExt.y - 3.0 * isoX + isoY) * 0.5;
    const float dyHi = ( 6.0 * hExt.y - 3.0 * isoX + isoY) * 0.5;
    const float dzLo = -3.0 * hExt.z - isoY;
    const float dzHi =  3.0 * hExt.z - isoY;

    dEntry = max(dxLo, max(dyLo, dzLo));
    dExit  = min(dxHi, min(dyHi, dzHi));
    return dEntry <= dExit;
}

inline void zSlabInterval(
    float isoY,
    float hZ,
    thread float& dLo,
    thread float& dHi
) {
    dLo = -3.0 * hZ - isoY;
    dHi =  3.0 * hZ - isoY;
}

inline bool circleDepthInterval(
    float isoX,
    float isoY,
    float R,
    thread float& dLo,
    thread float& dHi
) {
    const float disc = 18.0 * R * R - 9.0 * isoX * isoX;
    if (disc < 0.0) {
        return false;
    }
    const float halfRange = sqrt(disc);
    dLo = (isoY - halfRange) * 0.5;
    dHi = (isoY + halfRange) * 0.5;
    return true;
}

// |a*d + b| <= H solved for d. Degenerate (a == 0) returns either an empty
// or all-d slab depending on |b| vs H. Used by yaw-aware box and ellipsoid
// analytical depth searches where each axis has its own slope/offset.
inline bool slabFromLinear(
    float a,
    float b,
    float H,
    thread float& dLo,
    thread float& dHi
) {
    if (fabs(a) < 1e-10) {
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

// Yaw-aware box slab. pLocal_i(d) = a_i*d + b_i in shape-local coords, with
// pLocal = R_z(+visualYaw) . pView. z-axis is rotation-invariant under z-yaw.
inline bool boxSlabIntersectYaw(
    int2 isoRel,
    float3 hExt,
    float yawC,
    float yawS,
    thread float& dEntry,
    thread float& dExit
) {
    const float iX = float(isoRel.x);
    const float iY = float(isoRel.y);

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
    return dEntry <= dExit;
}

// Transform pView (view space) to pLocal (shape-local). Camera yaws by
// +visualYaw around +Z so world appears rotated by -visualYaw from view's
// POV; the inverse R_z(+visualYaw) rotates back to shape-local coords.
inline float3 viewToLocalYaw(float3 pView, float yawC, float yawS) {
    return float3(yawC * pView.x - yawS * pView.y,
                  yawS * pView.x + yawC * pView.y,
                  pView.z);
}

inline int boxDepthIntersect(int2 isoRel, float3 halfExtents, bool hollow) {
    const float isoX = float(isoRel.x);
    const float isoY = float(isoRel.y);

    float dEntry, dExit;
    if (!boxSlabIntersect(isoX, isoY, halfExtents + float3(0.5), dEntry, dExit)) {
        return kInvalidDepth;
    }

    if (!hollow) {
        int candidate = stableCeilToInt(dEntry);
        if (float(candidate) > dExit) {
            return kInvalidDepth;
        }
        const float3 p = isoToLocal3D(isoRel, float(candidate));
        if (sdfBox(p, halfExtents) <= 0.5 + kSdfBiasEpsilon) {
            return candidate;
        }
        if (float(candidate + 1) <= dExit) {
            return candidate + 1;
        }
        return kInvalidDepth;
    }

    const float3 hInt = halfExtents - float3(0.5);
    float dIntEntry = 1.0;
    float dIntExit = 0.0;
    if (hInt.x > 0.0 && hInt.y > 0.0 && hInt.z > 0.0) {
        boxSlabIntersect(isoX, isoY, hInt, dIntEntry, dIntExit);
    }

    int candidate = stableCeilToInt(dEntry);
    if (float(candidate) > dExit) {
        return kInvalidDepth;
    }
    if (dIntEntry > dIntExit || float(candidate) <= dIntEntry) {
        return candidate;
    }
    candidate = stableCeilToInt(dIntExit);
    if (float(candidate) <= dExit) {
        return candidate;
    }
    return kInvalidDepth;
}

// Yaw-aware box depth intersection. Same structure as boxDepthIntersect, but
// the slab and SDF eval are evaluated in shape-local coords (after rotating
// pView by R_z(+yaw)). At yaw=0 the formulas collapse to the unrotated path.
inline int boxDepthIntersectYaw(
    int2 isoRel,
    float3 halfExtents,
    bool hollow,
    float yawC,
    float yawS
) {
    float dEntry, dExit;
    if (!boxSlabIntersectYaw(isoRel, halfExtents + float3(0.5),
                             yawC, yawS, dEntry, dExit)) {
        return kInvalidDepth;
    }

    if (!hollow) {
        int candidate = stableCeilToInt(dEntry);
        if (float(candidate) > dExit) {
            return kInvalidDepth;
        }
        const float3 pLocal = viewToLocalYaw(
            isoToLocal3D(isoRel, float(candidate)), yawC, yawS);
        if (sdfBox(pLocal, halfExtents) <= 0.5 + kSdfBiasEpsilon) {
            return candidate;
        }
        if (float(candidate + 1) <= dExit) {
            return candidate + 1;
        }
        return kInvalidDepth;
    }

    const float3 hInt = halfExtents - float3(0.5);
    float dIntEntry = 1.0;
    float dIntExit = 0.0;
    if (hInt.x > 0.0 && hInt.y > 0.0 && hInt.z > 0.0) {
        boxSlabIntersectYaw(isoRel, hInt, yawC, yawS, dIntEntry, dIntExit);
    }

    int candidate = stableCeilToInt(dEntry);
    if (float(candidate) > dExit) {
        return kInvalidDepth;
    }
    if (dIntEntry > dIntExit || float(candidate) <= dIntEntry) {
        return candidate;
    }
    candidate = stableCeilToInt(dIntExit);
    if (float(candidate) <= dExit) {
        return candidate;
    }
    return kInvalidDepth;
}

inline int sphereDepthIntersect(int2 isoRel, float radius, bool hollow) {
    const float R = radius + 0.5;
    const float3 p0 = isoToLocal3D(isoRel, 0.0);
    const float tClosest = -(p0.x + p0.y + p0.z);
    const float3 pClosest = isoToLocal3D(isoRel, tClosest);
    const float perpDistSq = dot(pClosest, pClosest);

    if (perpDistSq > R * R) {
        return kInvalidDepth;
    }

    const float halfChord = sqrt(3.0 * (R * R - perpDistSq));
    const float tEntry = tClosest - halfChord;
    const float tExit  = tClosest + halfChord;

    if (!hollow) {
        int candidate = stableCeilToInt(tEntry);
        if (float(candidate) > tExit) {
            return kInvalidDepth;
        }
        const float3 p = isoToLocal3D(isoRel, float(candidate));
        if (sdfSphere(p, radius) <= 0.5 + kSdfBiasEpsilon) {
            return candidate;
        }
        if (float(candidate + 1) <= tExit) {
            return candidate + 1;
        }
        return kInvalidDepth;
    }

    const float Rint = radius - 0.5;
    float tIntEntry = tExit + 1.0;
    float tIntExit = tEntry - 1.0;
    if (Rint > 0.0 && perpDistSq <= Rint * Rint) {
        const float intHalfChord = sqrt(3.0 * (Rint * Rint - perpDistSq));
        tIntEntry = tClosest - intHalfChord;
        tIntExit  = tClosest + intHalfChord;
    }

    const int entryBase = stableCeilToInt(tEntry);
    for (int i = 0; i < 2; ++i) {
        const int candidate = entryBase + i;
        if (float(candidate) > min(tIntEntry, tExit)) {
            break;
        }
        const float3 p = isoToLocal3D(isoRel, float(candidate));
        const float sdf = sdfSphere(p, radius);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) {
            return candidate;
        }
    }
    const int exitBase = stableCeilToInt(tIntExit);
    for (int i = 0; i < 2; ++i) {
        const int candidate = exitBase + i;
        if (float(candidate) > tExit) {
            break;
        }
        const float3 p = isoToLocal3D(isoRel, float(candidate));
        const float sdf = sdfSphere(p, radius);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) {
            return candidate;
        }
    }
    return kInvalidDepth;
}

inline int cylinderDepthIntersect(
    int2 isoRel,
    float radius,
    float halfHeight,
    bool hollow
) {
    const float isoX = float(isoRel.x);
    const float isoY = float(isoRel.y);

    float dCircLo, dCircHi;
    if (!circleDepthInterval(isoX, isoY, radius + 0.5, dCircLo, dCircHi)) {
        return kInvalidDepth;
    }
    float dZLo, dZHi;
    zSlabInterval(isoY, halfHeight + 0.5, dZLo, dZHi);

    const float dEntry = max(dCircLo, dZLo);
    const float dExit  = min(dCircHi, dZHi);
    if (dEntry > dExit) {
        return kInvalidDepth;
    }

    if (!hollow) {
        int candidate = stableCeilToInt(dEntry);
        if (float(candidate) > dExit) {
            return kInvalidDepth;
        }
        const float3 p = isoToLocal3D(isoRel, float(candidate));
        if (sdfCylinder(p, radius, halfHeight) <= 0.5 + kSdfBiasEpsilon) {
            return candidate;
        }
        if (float(candidate + 1) <= dExit) {
            return candidate + 1;
        }
        return kInvalidDepth;
    }

    const float Rint = radius - 0.5;
    const float Hint = halfHeight - 0.5;
    float dIntEntry = dExit + 1.0;
    float dIntExit = dEntry - 1.0;
    if (Rint > 0.0 && Hint > 0.0) {
        float diCircLo, diCircHi;
        if (circleDepthInterval(isoX, isoY, Rint, diCircLo, diCircHi)) {
            float diZLo, diZHi;
            zSlabInterval(isoY, Hint, diZLo, diZHi);
            dIntEntry = max(diCircLo, diZLo);
            dIntExit  = min(diCircHi, diZHi);
        }
    }

    const int entryBase = stableCeilToInt(dEntry);
    for (int i = 0; i < 2; ++i) {
        const int candidate = entryBase + i;
        if (float(candidate) > min(dIntEntry, dExit)) {
            break;
        }
        const float3 p = isoToLocal3D(isoRel, float(candidate));
        const float sdf = sdfCylinder(p, radius, halfHeight);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) {
            return candidate;
        }
    }
    const int exitBase = stableCeilToInt(dIntExit);
    for (int i = 0; i < 2; ++i) {
        const int candidate = exitBase + i;
        if (float(candidate) > dExit) {
            break;
        }
        const float3 p = isoToLocal3D(isoRel, float(candidate));
        const float sdf = sdfCylinder(p, radius, halfHeight);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) {
            return candidate;
        }
    }
    return kInvalidDepth;
}

inline int ellipsoidDepthIntersect(int2 isoRel, float3 radii, bool hollow) {
    const float isoX = float(isoRel.x);
    const float isoY = float(isoRel.y);

    const float3 R = radii + float3(0.5);
    const float ax = 1.0 / (3.0 * R.x);
    const float bx = (-3.0 * isoX - isoY) / (6.0 * R.x);
    const float ay = 1.0 / (3.0 * R.y);
    const float by = (3.0 * isoX - isoY) / (6.0 * R.y);
    const float az = 1.0 / (3.0 * R.z);
    const float bz = isoY / (3.0 * R.z);

    const float A = ax * ax + ay * ay + az * az;
    const float B = 2.0 * (ax * bx + ay * by + az * bz);
    const float C = bx * bx + by * by + bz * bz - 1.0;

    const float disc = B * B - 4.0 * A * C;
    if (disc < 0.0) {
        return kInvalidDepth;
    }

    const float sqrtDisc = sqrt(disc);
    const float inv2A = 0.5 / A;
    const float dEntry = (-B - sqrtDisc) * inv2A;
    const float dExit  = (-B + sqrtDisc) * inv2A;

    if (!hollow) {
        const int entryBase = stableCeilToInt(dEntry);
        for (int i = 0; i < 3; ++i) {
            const int candidate = entryBase + i;
            if (float(candidate) > dExit) {
                return kInvalidDepth;
            }
            const float3 p = isoToLocal3D(isoRel, float(candidate));
            if (sdfEllipsoid(p, radii) <= 0.5 + kSdfBiasEpsilon) {
                return candidate;
            }
        }
        return kInvalidDepth;
    }

    const float3 Rint = radii - float3(0.5);
    float dIntEntry = dExit + 1.0;
    float dIntExit = dEntry - 1.0;
    if (Rint.x > 0.0 && Rint.y > 0.0 && Rint.z > 0.0) {
        const float iax = 1.0 / (3.0 * Rint.x);
        const float ibx = (-3.0 * isoX - isoY) / (6.0 * Rint.x);
        const float iay = 1.0 / (3.0 * Rint.y);
        const float iby = (3.0 * isoX - isoY) / (6.0 * Rint.y);
        const float iaz = 1.0 / (3.0 * Rint.z);
        const float ibz = isoY / (3.0 * Rint.z);

        const float iA = iax * iax + iay * iay + iaz * iaz;
        const float iB = 2.0 * (iax * ibx + iay * iby + iaz * ibz);
        const float iC = ibx * ibx + iby * iby + ibz * ibz - 1.0;

        const float iDisc = iB * iB - 4.0 * iA * iC;
        if (iDisc >= 0.0) {
            const float iSqrt = sqrt(iDisc);
            const float iInv2A = 0.5 / iA;
            dIntEntry = (-iB - iSqrt) * iInv2A;
            dIntExit  = (-iB + iSqrt) * iInv2A;
        }
    }

    const int entryBase = stableCeilToInt(dEntry);
    for (int i = 0; i < 3; ++i) {
        const int candidate = entryBase + i;
        if (float(candidate) > min(dIntEntry, dExit)) {
            break;
        }
        const float3 p = isoToLocal3D(isoRel, float(candidate));
        const float sdf = sdfEllipsoid(p, radii);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) {
            return candidate;
        }
    }
    const int exitBase = stableCeilToInt(dIntExit);
    for (int i = 0; i < 3; ++i) {
        const int candidate = exitBase + i;
        if (float(candidate) > dExit) {
            break;
        }
        const float3 p = isoToLocal3D(isoRel, float(candidate));
        const float sdf = sdfEllipsoid(p, radii);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) {
            return candidate;
        }
    }
    return kInvalidDepth;
}

// Yaw-aware ellipsoid depth intersection. Each axis's pLocal_i(d) = a_i*d+b_i
// is scaled by 1/R_i to feed the same |q|^2 = 1 quadratic in d as the
// unrotated path. The per-axis numerators are independent of R, so the outer
// shell (R = radii + 0.5) and inner shell (R = radii - 0.5) reuse them. At
// yaw=0 the per-axis (a, b) collapse to the unrotated formulas exactly.
inline int ellipsoidDepthIntersectYaw(
    int2 isoRel,
    float3 radii,
    bool hollow,
    float yawC,
    float yawS
) {
    const float iX = float(isoRel.x);
    const float iY = float(isoRel.y);

    const float3 aNum = float3((yawC - yawS) / 3.0,
                               (yawC + yawS) / 3.0,
                               1.0 / 3.0);
    const float3 bNum = float3(-(yawC + yawS) * 0.5 * iX - (yawC - yawS) * iY / 6.0,
                                (yawC - yawS) * 0.5 * iX - (yawC + yawS) * iY / 6.0,
                                iY / 3.0);

    const float3 R = radii + float3(0.5);
    const float3 a = aNum / R;
    const float3 b = bNum / R;

    const float A = dot(a, a);
    const float B = 2.0 * dot(a, b);
    const float C = dot(b, b) - 1.0;

    const float disc = B * B - 4.0 * A * C;
    if (disc < 0.0) {
        return kInvalidDepth;
    }

    const float sqrtDisc = sqrt(disc);
    const float inv2A = 0.5 / A;
    const float dEntry = (-B - sqrtDisc) * inv2A;
    const float dExit  = (-B + sqrtDisc) * inv2A;

    if (!hollow) {
        const int entryBase = stableCeilToInt(dEntry);
        for (int i = 0; i < 3; ++i) {
            const int candidate = entryBase + i;
            if (float(candidate) > dExit) {
                return kInvalidDepth;
            }
            const float3 pLocal = viewToLocalYaw(
                isoToLocal3D(isoRel, float(candidate)), yawC, yawS);
            if (sdfEllipsoid(pLocal, radii) <= 0.5 + kSdfBiasEpsilon) {
                return candidate;
            }
        }
        return kInvalidDepth;
    }

    const float3 Rint = radii - float3(0.5);
    float dIntEntry = dExit + 1.0;
    float dIntExit = dEntry - 1.0;
    if (Rint.x > 0.0 && Rint.y > 0.0 && Rint.z > 0.0) {
        const float3 ia = aNum / Rint;
        const float3 ib = bNum / Rint;

        const float iA = dot(ia, ia);
        const float iB = 2.0 * dot(ia, ib);
        const float iC = dot(ib, ib) - 1.0;

        const float iDisc = iB * iB - 4.0 * iA * iC;
        if (iDisc >= 0.0) {
            const float iSqrt = sqrt(iDisc);
            const float iInv2A = 0.5 / iA;
            dIntEntry = (-iB - iSqrt) * iInv2A;
            dIntExit  = (-iB + iSqrt) * iInv2A;
        }
    }

    const int entryBase = stableCeilToInt(dEntry);
    for (int i = 0; i < 3; ++i) {
        const int candidate = entryBase + i;
        if (float(candidate) > min(dIntEntry, dExit)) {
            break;
        }
        const float3 pLocal = viewToLocalYaw(
            isoToLocal3D(isoRel, float(candidate)), yawC, yawS);
        const float sdf = sdfEllipsoid(pLocal, radii);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) {
            return candidate;
        }
    }
    const int exitBase = stableCeilToInt(dIntExit);
    for (int i = 0; i < 3; ++i) {
        const int candidate = exitBase + i;
        if (float(candidate) > dExit) {
            break;
        }
        const float3 pLocal = viewToLocalYaw(
            isoToLocal3D(isoRel, float(candidate)), yawC, yawS);
        const float sdf = sdfEllipsoid(pLocal, radii);
        if (sdf <= 0.5 + kSdfBiasEpsilon && sdf >= -0.5 - kSdfBiasEpsilon) {
            return candidate;
        }
    }
    return kInvalidDepth;
}

inline int generalDepthSearch(
    int2 isoRel,
    uint shapeType,
    float4 params,
    bool hollow,
    float dExtent
) {
    const int dMin = int(floor(-dExtent));
    const int dMax = int(ceil(dExtent));
    for (int d = dMin; d <= dMax; d += 1) {
        const float3 p = isoToLocal3D(isoRel, float(d));
        const float sdf = evaluateSDF(p, shapeType, params);
        if (sdf <= 0.5 + kSdfBiasEpsilon &&
            (!hollow || sdf >= -0.5 - kSdfBiasEpsilon)) {
            return d;
        }
    }
    return kInvalidDepth;
}

// Yaw-aware general SDF depth search. Mirrors the GLSL counterpart in
// c_shapes_to_trixel.glsl. The iso projection is fixed in view space, but
// the SDF's local frame is world-aligned. Camera yaw rotates the world by
// -visualYaw from the view's POV, so the world-local query point is the
// view-local point rotated by +visualYaw around Z. Analytical paths bake
// in yaw=0 in their interval derivations, so this brute-force search is
// the only correct path at non-zero yaw.
inline int generalDepthSearchYaw(
    int2 isoRel,
    uint shapeType,
    float4 params,
    bool hollow,
    float dExtent,
    float yawC,
    float yawS
) {
    const int dMin = int(floor(-dExtent));
    const int dMax = int(ceil(dExtent));
    for (int d = dMin; d <= dMax; d += 1) {
        const float3 pView = isoToLocal3D(isoRel, float(d));
        const float3 p = float3(yawC * pView.x - yawS * pView.y,
                                yawS * pView.x + yawC * pView.y,
                                pView.z);
        const float sdf = evaluateSDF(p, shapeType, params);
        if (sdf <= 0.5 + kSdfBiasEpsilon &&
            (!hollow || sdf >= -0.5 - kSdfBiasEpsilon)) {
            return d;
        }
    }
    return kInvalidDepth;
}

// Snap-mode lattice walk. At sub=1 + yaw=0 the analytical SDF entry point
// isn't on the integer lattice, which can miss the true front-most voxel; so
// we walk the (isoX + isoY) even sublattice in steps of 3 along the iso
// column and take the first hit. Matches CPU voxel-pool carving exactly.
// Caller must gate on yawZero — at non-zero yaw the iso lattice no longer
// aligns with world voxels (the trixel raster takes the cardinal-snap path
// per T-055/T-058, so SDF/voxel-pool divergence is expected anyway).
inline int snapLatticeWalk(
    int2 isoPixelRel,
    uint shapeType,
    float4 paramsScaled,
    float dExtent
) {
    if (((isoPixelRel.x + isoPixelRel.y) & 1) != 0) {
        return kInvalidDepth;
    }
    const int isoY = isoPixelRel.y;
    const int dMin = int(floor(-dExtent)) - 3;
    const int dMax = int(ceil(dExtent)) + 3;
    const int rem = ((dMin + isoY) % 3 + 3) % 3;
    const int dStart = dMin + ((3 - rem) % 3);
    for (int d = dStart; d <= dMax; d += 3) {
        const float3 p = isoToLocal3D(isoPixelRel, float(d));
        const int3 voxelPos = int3(round(p));
        const int2 projected = pos3DtoPos2DIso(voxelPos);
        if (projected.x != isoPixelRel.x || projected.y != isoPixelRel.y) {
            continue;
        }
        if (evaluateSDF(float3(voxelPos), shapeType, paramsScaled) <= 0.5) {
            return voxelPos.x + voxelPos.y + voxelPos.z;
        }
    }
    return kInvalidDepth;
}

// O(1) surface depth dispatcher with continuous Z-yaw support.
// - Sphere: rotation-invariant (|p| under z-yaw unchanged); analytical works
//   at any yaw without modification.
// - Cylinder: z-axis aligned, |p.xy| invariant under z-yaw; same.
// - Box, ellipsoid: shape-axes don't align with view-axes under yaw; the
//   yaw-aware variant re-derives the per-axis linear coefficients.
// - All other shapes: general SDF search (yaw-aware via R_z(+yaw) on the
//   query point). At yaw=0 each branch collapses to the original code path,
//   keeping reference renders pixel-stable.
inline int findSurfaceDepth(
    int2 isoRel,
    uint shapeType,
    float4 params,
    uint flags,
    float dExtent,
    float yawC,
    float yawS
) {
    const bool hollow = (flags & FLAG_HOLLOW) != 0u;
    const float3 halfSize = params.xyz * 0.5;
    const bool yawZero = (yawC == 1.0 && yawS == 0.0);

    if (shapeType == SHAPE_SPHERE) {
        return sphereDepthIntersect(isoRel, params.x, hollow);
    }
    if (shapeType == SHAPE_CYLINDER) {
        return cylinderDepthIntersect(isoRel, params.x, halfSize.z, hollow);
    }
    if (shapeType == SHAPE_BOX) {
        return yawZero
            ? boxDepthIntersect(isoRel, halfSize, hollow)
            : boxDepthIntersectYaw(isoRel, halfSize, hollow, yawC, yawS);
    }
    if (shapeType == SHAPE_ELLIPSOID) {
        return yawZero
            ? ellipsoidDepthIntersect(isoRel, halfSize, hollow)
            : ellipsoidDepthIntersectYaw(isoRel, halfSize, hollow, yawC, yawS);
    }
    return yawZero
        ? generalDepthSearch(isoRel, shapeType, params, hollow, dExtent)
        : generalDepthSearchYaw(isoRel, shapeType, params, hollow, dExtent,
                                yawC, yawS);
}

// ---------- Kernel ----------

kernel void c_shapes_to_trixel(
    constant ShapesFrameData& frameData [[buffer(23)]],
    device const ShapeDescriptor* shapes [[buffer(20)]],
    device const ShapeTileDescriptor* tiles [[buffer(30)]],
    device atomic_int* distanceScratch [[buffer(16)]],
    texture2d<float, access::write> triangleCanvasColors [[texture(0)]],
    texture2d<int, access::write> triangleCanvasDistances [[texture(1)]],
    texture2d<uint, access::write> triangleCanvasEntityIds [[texture(2)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    const ShapeTileDescriptor tile = tiles[groupId.x];
    const int shapeIndex = tile.shapeIndex;
    const int2 isoOrigin = tile.tileIsoOrigin;
    const ShapeDescriptor shape = shapes[shapeIndex];

    // Continuous Z-yaw consumed by the SDF path. Mirrors the GLSL shader in
    // c_shapes_to_trixel.glsl. At yaw=0 every line below collapses to the
    // original code (rotation is identity); the bool gate keeps the
    // analytical fast paths in scope at exactly yaw=0 so reference images
    // remain pixel-stable. Ternary on the uniform-driven yawZero skips the
    // transcendental dispatch entirely at yaw=0 instead of computing
    // cos/sin unconditionally.
    const bool yawZero = (frameData.visualYaw == 0.0);
    const float yawC = yawZero ? 1.0 : cos(frameData.visualYaw);
    const float yawS = yawZero ? 0.0 : sin(frameData.visualYaw);

    const float3 worldPos = shape.worldPosition.xyz;
    // viewPos = R_z(-visualYaw) · worldPos. Camera yaws by +visualYaw, so
    // world coords appear rotated by -visualYaw from the view's POV.
    const float3 viewPos = yawZero
        ? worldPos
        : float3( yawC * worldPos.x + yawS * worldPos.y,
                 -yawS * worldPos.x + yawC * worldPos.y,
                  worldPos.z);
    const int3 origin = int3(round(viewPos));

    const int renderMode = frameData.voxelRenderOptions.x;
    const int subdivisions = max(frameData.voxelRenderOptions.y, 1);
    // Smooth mode degenerates to snap when there is nothing to smooth.  At
    // sub == 1 the analytical path would paint a 2x3 diamond at every iso
    // pixel (both parities), producing overlapping diamonds that alias into
    // the voxel-pool tiling.  Route sub==1 through the exact lattice walk
    // used by snap mode so shapes at minimum zoom match C_VoxelSetNew output
    // trixel-for-trixel.
    const bool smoothMode = (renderMode != 0) && (subdivisions > 1);
    const int sub = smoothMode ? subdivisions : 1;

    const int3 originScaled = origin * sub;
    const float3 effectiveSize = (shape.shapeType == SHAPE_BOX)
        ? shape.params.xyz - 1.0
        : shape.params.xyz;
    const float4 paramsScaled =
        float4(effectiveSize * float(sub), shape.params.w);

    float3 boundingHalf;
    const uint st = shape.shapeType;
    if (st == SHAPE_SPHERE) {
        boundingHalf = float3(paramsScaled.x);
    } else if (st == SHAPE_CYLINDER || st == SHAPE_CONE) {
        boundingHalf = float3(paramsScaled.x, paramsScaled.x, paramsScaled.z * 0.5);
    } else if (st == SHAPE_TORUS) {
        const float xyR = paramsScaled.x + paramsScaled.y;
        boundingHalf = float3(xyR, xyR, paramsScaled.y);
    } else if (st == SHAPE_CURVED_PANEL) {
        float3 hs = paramsScaled.xyz * 0.5;
        hs.z += abs(paramsScaled.w) * hs.x;
        boundingHalf = hs;
    } else {
        boundingHalf = paramsScaled.xyz * 0.5;
    }
    // After Z-yaw the shape's view-space AABB grows in XY by |c|·hX + |s|·hY
    // (and symmetrically for Y). Use this expanded half-extent for the iso
    // footprint check and the generalDepthSearch range so the full rotated
    // shape stays inside the search window.
    float3 boundingHalfView;
    if (yawZero) {
        boundingHalfView = boundingHalf;
    } else {
        const float absC = abs(yawC);
        const float absS = abs(yawS);
        boundingHalfView = float3(boundingHalf.x * absC + boundingHalf.y * absS,
                                  boundingHalf.x * absS + boundingHalf.y * absC,
                                  boundingHalf.z);
    }
    const int3 extentScaled = int3(ceil(boundingHalfView)) + int3(1);

    const int2 originIsoScaled = pos3DtoPos2DIso(originScaled);
    const int2 isoExtentScaled = int2(
        extentScaled.x + extentScaled.y,
        extentScaled.x + extentScaled.y + 2 * extentScaled.z
    );

    const float dExtent = float(extentScaled.x + extentScaled.y + extentScaled.z);

    const int2 pixelCoord = isoOrigin + int2(localId.xy);

    const int2 frameOffset =
        frameData.trixelCanvasOffsetZ1 +
        int2(floor(frameData.frameCanvasOffset * float(sub)));

    const int2 baseCanvasPixel = frameOffset + pixelCoord;
    if (baseCanvasPixel.x < -3 || baseCanvasPixel.x >= frameData.canvasSize.x + 3 ||
        baseCanvasPixel.y < -3 || baseCanvasPixel.y >= frameData.canvasSize.y + 3) {
        return;
    }

    const int2 isoPixelRel = pixelCoord - originIsoScaled;

    if (abs(isoPixelRel.x) > isoExtentScaled.x + 2 ||
        abs(isoPixelRel.y) > isoExtentScaled.y + 2) {
        return;
    }

    // Snap-mode lattice walk only at yaw=0 — that's where SDF must align
    // with the CPU voxel-pool carve trixel-for-trixel. At any non-zero yaw
    // the trixel raster takes the cardinal-snap path (T-055) and screen-
    // space residual rotation (T-058), so falling through to the analytical
    // / general path is correct: those produce a continuous-yaw surface that
    // matches the (snap + residual) trixel render once T-055 + T-058 land.
    const int surfaceD = (yawZero && !smoothMode)
        ? snapLatticeWalk(isoPixelRel, shape.shapeType, paramsScaled, dExtent)
        : findSurfaceDepth(isoPixelRel, shape.shapeType, paramsScaled,
                           shape.flags, dExtent, yawC, yawS);
    if (surfaceD == kInvalidDepth) {
        return;
    }

    const int originDistance = originScaled.x + originScaled.y + originScaled.z;
    const int baseDepth = surfaceD + originDistance;
    float4 baseColor = unpackColor(shape.color);

    if ((shape.flags & FLAG_DEPTH_COLOR) != 0u) {
        // dExtent above includes a +1 per-axis safety margin for the
        // lattice walk; use the unpadded view-space half-extent sum
        // (boundingHalfView) so the hue range matches the rotated
        // shape's actual iso-depth extent at any yaw.  Identical to
        // the unrotated boundingHalf sum at yaw=0.
        const float dColor = boundingHalfView.x + boundingHalfView.y + boundingHalfView.z;
        const float denomC = max((4.0 / 3.0) * dColor, 1.0);
        const float t = clamp((float(surfaceD) + dColor) / denomC, 0.0, 1.0);
        baseColor.rgb = hsvToRgb(float3(0.66 * t, 1.0, 1.0));
    } else if ((shape.flags & FLAG_CHECKERBOARD) != 0u) {
        // Checker at EFFECTIVE-voxel (scaled-integer) granularity — as
        // sub grows (zoom/subdivision), the cube gets more cells, no
        // collapse to physical voxels. Integer ops are bit-exact across
        // frames (no FMA flicker).
        const int iX = isoPixelRel.x;
        const int iY = isoPixelRel.y;
        const int d  = surfaceD;
        const int nx6 = 2 * d - 3 * iX - iY;
        const int ny6 = 2 * d + 3 * iX - iY;
        const int nz6 = 2 * d + 2 * iY;
        const int sx = (nx6 >= 0) ? (nx6 + 3) / 6 : -((-nx6 + 3) / 6);
        const int sy = (ny6 >= 0) ? (ny6 + 3) / 6 : -((-ny6 + 3) / 6);
        const int sz = (nz6 >= 0) ? (nz6 + 3) / 6 : -((-nz6 + 3) / 6);
        // (sx, sy, sz) above is the recovered cell index in VIEW coords.
        // Under camera yaw the shape's checker pattern is in world coords
        // (it lives on the SDF, which we evaluated at the rotated point),
        // so rotate (sx, sy) by +visualYaw to recover the world-coord cell
        // before the parity test. At yaw=0 this is identity and the
        // existing integer-only path is preserved bit-exact.
        int parity;
        if (yawZero) {
            parity = (sx + sy + sz) & 1;
        } else {
            const float wx = yawC * float(sx) - yawS * float(sy);
            const float wy = yawS * float(sx) + yawC * float(sy);
            const int wxi = int(floor(wx + 0.5));
            const int wyi = int(floor(wy + 0.5));
            parity = (wxi + wyi + sz) & 1;
        }
        if (parity != 0) {
            baseColor.rgb *= 0.55;
        }
    }

    for (int face = 0; face < 3; ++face) {
        const int depthEncoded = encodeDepthWithFace(baseDepth, face);
        const float4 col = adjustColorForFace(baseColor, face);

        for (int subPixel = 0; subPixel < 2; ++subPixel) {
            const int2 offset = faceOffset_2x3(face, subPixel);
            const int2 canvasPixel = baseCanvasPixel + offset;

            if (!isInsideCanvas(canvasPixel, frameData.canvasSize)) {
                continue;
            }

            const uint linearIndex =
                uint(canvasPixel.y) * uint(frameData.canvasSize.x) +
                uint(canvasPixel.x);

            if (frameData.passIndex == 0) {
                atomic_fetch_min_explicit(
                    &distanceScratch[linearIndex],
                    depthEncoded,
                    memory_order_relaxed
                );
            } else {
                const int stored = atomic_load_explicit(
                    &distanceScratch[linearIndex],
                    memory_order_relaxed
                );
                if (depthEncoded == stored) {
                    const uint2 pix = uint2(canvasPixel);
                    triangleCanvasColors.write(col, pix);
                    triangleCanvasDistances.write(
                        int4(depthEncoded, 0, 0, 0), pix);
                    triangleCanvasEntityIds.write(
                        uint4(shape.entityId, 0u, 0u, 0u), pix);
                }
            }
        }
    }
}
